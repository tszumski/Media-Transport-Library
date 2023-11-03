/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Intel Corporation
 */

#include "mt_shared_rss.h"

#include "../dev/mt_af_xdp.h"
#include "../mt_log.h"
#include "../mt_sch.h"
#include "../mt_stat.h"
#include "../mt_util.h"

#define MT_SRSS_BURST_SIZE (128)
#define MT_SRSS_RING_PREFIX "SR_"

static inline void srss_lock(struct mt_srss_impl* srss) {
  rte_spinlock_lock(&srss->mutex);
}

/* return true if try lock succ */
static inline bool srss_try_lock(struct mt_srss_impl* srss) {
  int ret = rte_spinlock_trylock(&srss->mutex);
  return ret ? true : false;
}

static inline void srss_unlock(struct mt_srss_impl* srss) {
  rte_spinlock_unlock(&srss->mutex);
}

static inline void srss_entry_pkts_enqueue(struct mt_srss_entry* entry,
                                           struct rte_mbuf** pkts,
                                           const uint16_t nb_pkts) {
  /* use bulk version */
  unsigned int n = rte_ring_sp_enqueue_bulk(entry->ring, (void**)pkts, nb_pkts, NULL);
  entry->stat_enqueue_cnt += n;
  if (n == 0) {
    rte_pktmbuf_free_bulk(pkts, nb_pkts);
    entry->stat_enqueue_fail_cnt += nb_pkts;
  }
}

#define UPDATE_ENTRY()                                                             \
  do {                                                                             \
    if (matched_pkts_nb)                                                           \
      srss_entry_pkts_enqueue(last_srss_entry, &matched_pkts[0], matched_pkts_nb); \
    last_srss_entry = srss_entry;                                                  \
    matched_pkts_nb = 0;                                                           \
  } while (0)

#define CNI_ENQUEUE()                                        \
  do {                                                       \
    if (srss->cni_entry)                                     \
      srss_entry_pkts_enqueue(srss->cni_entry, &pkts[i], 1); \
    else                                                     \
      rte_pktmbuf_free(pkts[i]);                             \
  } while (0)

static int srss_tasklet_handler(void* priv) {
  struct mt_srss_impl* srss = priv;
  struct mtl_main_impl* impl = srss->parent;
  struct mt_interface* inf = mt_if(impl, srss->port);
  struct rte_mbuf *pkts[MT_SRSS_BURST_SIZE], *matched_pkts[MT_SRSS_BURST_SIZE];
  struct mt_srss_entry *srss_entry, *last_srss_entry;
  struct mt_udp_hdr* hdr;
  struct rte_ipv4_hdr* ipv4;
  struct rte_udp_hdr* udp;

  for (uint16_t queue = 0; queue < inf->nb_rx_q; queue++) {
    uint16_t matched_pkts_nb = 0;

    uint16_t rx;
    if (srss->xdps) {
      rx = mt_rx_xdp_burst(srss->xdps[queue], pkts, MT_SRSS_BURST_SIZE);
    } else {
      rx =
          rte_eth_rx_burst(mt_port_id(impl, srss->port), queue, pkts, MT_SRSS_BURST_SIZE);
    }
    if (!rx) continue;

    srss_lock(srss);
    last_srss_entry = NULL;
    for (uint16_t i = 0; i < rx; i++) {
      srss_entry = NULL;
      hdr = rte_pktmbuf_mtod(pkts[i], struct mt_udp_hdr*);
      if (hdr->eth.ether_type !=
          htons(RTE_ETHER_TYPE_IPV4)) { /* non ip, redirect to cni */
        UPDATE_ENTRY();
        CNI_ENQUEUE();
        continue;
      }
      ipv4 = &hdr->ipv4;
      if (ipv4->next_proto_id != IPPROTO_UDP) { /* non udp, redirect to cni */
        UPDATE_ENTRY();
        CNI_ENQUEUE();
        continue;
      }
      udp = &hdr->udp;
      MT_TAILQ_FOREACH(srss_entry, &srss->head, next) {
        bool ip_matched;
        if (srss_entry->flow.flags & MT_RXQ_FLOW_F_NO_IP) {
          ip_matched = true;
        } else {
          ip_matched = mt_is_multicast_ip(srss_entry->flow.dip_addr)
                           ? (ipv4->dst_addr == *(uint32_t*)srss_entry->flow.dip_addr)
                           : (ipv4->src_addr == *(uint32_t*)srss_entry->flow.dip_addr);
        }
        bool port_matched;
        if (srss_entry->flow.flags & MT_RXQ_FLOW_F_NO_PORT) {
          port_matched = true;
        } else {
          port_matched = ntohs(udp->dst_port) == srss_entry->flow.dst_port;
        }
        if (ip_matched && port_matched) { /* match dst ip:port */
          if (srss_entry != last_srss_entry) UPDATE_ENTRY();
          matched_pkts[matched_pkts_nb++] = pkts[i];
          break;
        }
      }
      if (!srss_entry) { /* no match, redirect to cni */
        UPDATE_ENTRY();
        CNI_ENQUEUE();
      }
    }
    if (matched_pkts_nb)
      srss_entry_pkts_enqueue(last_srss_entry, &matched_pkts[0], matched_pkts_nb);
    srss_unlock(srss);
  }

  return 0;
}

static void* srss_traffic_thread(void* arg) {
  struct mt_srss_impl* srss = arg;

  info("%s, start\n", __func__);
  while (rte_atomic32_read(&srss->stop_thread) == 0) {
    srss_tasklet_handler(srss);
    mt_sleep_ms(1);
  }
  info("%s, stop\n", __func__);

  return NULL;
}

static int srss_traffic_thread_start(struct mt_srss_impl* srss) {
  int ret;

  if (srss->tid) {
    err("%s, srss_traffic thread already start\n", __func__);
    return 0;
  }

  rte_atomic32_set(&srss->stop_thread, 0);
  ret = pthread_create(&srss->tid, NULL, srss_traffic_thread, srss);
  if (ret < 0) {
    err("%s, srss_traffic thread create fail %d\n", __func__, ret);
    return ret;
  }

  return 0;
}

static int srss_traffic_thread_stop(struct mt_srss_impl* srss) {
  rte_atomic32_set(&srss->stop_thread, 1);
  if (srss->tid) {
    pthread_join(srss->tid, NULL);
    srss->tid = 0;
  }

  return 0;
}

static int srss_tasklet_start(void* priv) {
  struct mt_srss_impl* srss = priv;

  /* tasklet will take over the srss thread */
  srss_traffic_thread_stop(srss);

  return 0;
}

static int srss_tasklet_stop(void* priv) {
  struct mt_srss_impl* srss = priv;

  srss_traffic_thread_start(srss);

  return 0;
}

static int srss_stat(void* priv) {
  struct mt_srss_impl* srss = priv;
  enum mtl_port port = srss->port;
  struct mt_srss_entry* entry;
  int idx;

  if (!srss_try_lock(srss)) {
    notice("%s(%d), get lock fail\n", __func__, port);
    return 0;
  }
  MT_TAILQ_FOREACH(entry, &srss->head, next) {
    idx = entry->idx;
    notice("%s(%d,%d), enqueue %u dequeue %u\n", __func__, port, idx,
           entry->stat_enqueue_cnt, entry->stat_dequeue_cnt);
    entry->stat_enqueue_cnt = 0;
    entry->stat_dequeue_cnt = 0;
    if (entry->stat_enqueue_fail_cnt) {
      warn("%s(%d,%d), enqueue fail %u\n", __func__, port, idx,
           entry->stat_enqueue_fail_cnt);
      entry->stat_enqueue_fail_cnt = 0;
    }
  }
  srss_unlock(srss);

  return 0;
}

static int srss_uinit_xdp(struct mt_srss_impl* srss) {
  struct mtl_main_impl* impl = srss->parent;
  enum mtl_port port = srss->port;
  struct mt_interface* inf = mt_if(impl, port);

  struct mt_rx_xdp_entry** xdps = srss->xdps;
  if (!xdps) return 0;

  for (uint16_t queue = 0; queue < inf->nb_rx_q; queue++) {
    if (xdps[queue]) {
      mt_rx_xdp_put(xdps[queue]);
      xdps[queue] = NULL;
    }
  }

  mt_rte_free(xdps);
  srss->xdps = NULL;
  return 0;
}

static int srss_init_xdp(struct mt_srss_impl* srss) {
  struct mtl_main_impl* impl = srss->parent;
  enum mtl_port port = srss->port;
  struct mt_interface* inf = mt_if(impl, port);

  srss->xdps =
      mt_rte_zmalloc_socket(sizeof(*srss->xdps) * inf->nb_rx_q, mt_socket_id(impl, port));
  if (!srss->xdps) {
    err("%s(%d), xdps malloc fail\n", __func__, port);
    return -ENOMEM;
  }

  struct mt_rxq_flow flow;
  memset(&flow, 0, sizeof(flow));
  struct mt_rx_xdp_get_args args;
  memset(&args, 0, sizeof(args));
  args.queue_match = true;
  args.skip_flow = true;
  args.skip_udp_port_check = true;

  for (uint16_t queue = 0; queue < inf->nb_rx_q; queue++) {
    /* get a 1:1 mapped queue */
    args.queue_id = queue;
    srss->xdps[queue] = mt_rx_xdp_get(impl, port, &flow, &args);
    if (!srss->xdps[queue]) {
      err("%s(%d), xdp queue %u get fail\n", __func__, port, queue);
      srss_uinit_xdp(srss);
      return -ENOMEM;
    }
  }

  return 0;
}

struct mt_srss_entry* mt_srss_get(struct mtl_main_impl* impl, enum mtl_port port,
                                  struct mt_rxq_flow* flow) {
  struct mt_srss_impl* srss = impl->srss[port];
  int idx = srss->entry_idx;
  struct mt_srss_entry* entry;

  if (!mt_has_srss(impl, port)) {
    err("%s(%d,%d), shared rss not enabled\n", __func__, port, idx);
    return NULL;
  }

  MT_TAILQ_FOREACH(entry, &srss->head, next) {
    if (entry->flow.dst_port == flow->dst_port &&
        *(uint32_t*)entry->flow.dip_addr == *(uint32_t*)flow->dip_addr) {
      err("%s(%d,%d), already has entry %u.%u.%u.%u:%u\n", __func__, port, idx,
          flow->dip_addr[0], flow->dip_addr[1], flow->dip_addr[2], flow->dip_addr[3],
          flow->dst_port);
      return NULL;
    }
  }

  entry = mt_rte_zmalloc_socket(sizeof(*entry), mt_socket_id(impl, port));
  if (!entry) {
    err("%s(%d,%d), malloc fail\n", __func__, port, idx);
    return NULL;
  }

  /* ring create */
  char ring_name[32];
  snprintf(ring_name, 32, "%sP%d_%d", MT_SRSS_RING_PREFIX, port, idx);
  entry->ring = rte_ring_create(ring_name, 512, mt_socket_id(impl, MTL_PORT_P),
                                RING_F_SP_ENQ | RING_F_SC_DEQ);
  if (!entry->ring) {
    err("%s(%d,%d), ring create fail\n", __func__, port, idx);
    mt_rte_free(entry);
    return NULL;
  }

  entry->flow = *flow;
  entry->srss = srss;
  entry->idx = idx;

  srss_lock(srss);
  MT_TAILQ_INSERT_TAIL(&srss->head, entry, next);
  if (flow->flags & MT_RXQ_FLOW_F_SYS_QUEUE) srss->cni_entry = entry;
  srss->entry_idx++;
  srss_unlock(srss);

  info("%s(%d), entry %u.%u.%u.%u:(dst)%u on %d\n", __func__, port, flow->dip_addr[0],
       flow->dip_addr[1], flow->dip_addr[2], flow->dip_addr[3], flow->dst_port, idx);
  return entry;
}

int mt_srss_put(struct mt_srss_entry* entry) {
  struct mt_srss_impl* srss = entry->srss;
  enum mtl_port port = srss->port;

  srss_lock(srss);
  MT_TAILQ_REMOVE(&srss->head, entry, next);
  srss_unlock(srss);

  if (entry->ring) {
    mt_ring_dequeue_clean(entry->ring);
    rte_ring_free(entry->ring);
    entry->ring = NULL;
  }

  info("%s(%d), succ on %d\n", __func__, port, entry->idx);
  mt_rte_free(entry);
  return 0;
}

int mt_srss_init(struct mtl_main_impl* impl) {
  int num_ports = mt_num_ports(impl);
  int ret;

  for (int i = 0; i < num_ports; i++) {
    if (!mt_has_srss(impl, i)) continue;

    impl->srss[i] = mt_rte_zmalloc_socket(sizeof(*impl->srss[i]), mt_socket_id(impl, i));
    if (!impl->srss[i]) {
      err("%s(%d), srss malloc fail\n", __func__, i);
      mt_srss_uinit(impl);
      return -ENOMEM;
    }
    struct mt_srss_impl* srss = impl->srss[i];

    struct mt_sch_impl* sch = mt_sch_get(impl, 0, MT_SCH_TYPE_DEFAULT, MT_SCH_MASK_ALL);
    if (!sch) {
      err("%s(%d), get sch fail\n", __func__, i);
      mt_srss_uinit(impl);
      return -EIO;
    }

    srss->sch = sch;
    srss->port = i;
    srss->parent = impl;
    srss->queue_mode =
        mt_pmd_is_native_af_xdp(impl, i) ? MT_QUEUE_MODE_XDP : MT_QUEUE_MODE_DPDK;
    MT_TAILQ_INIT(&srss->head);

    if (srss->queue_mode == MT_QUEUE_MODE_XDP) {
      ret = srss_init_xdp(srss);
      if (ret < 0) {
        err("%s(%d), init xdp fail\n", __func__, i);
        mt_srss_uinit(impl);
        return ret;
      }
    }

    struct mt_sch_tasklet_ops ops;
    memset(&ops, 0x0, sizeof(ops));
    ops.priv = srss;
    ops.name = "shared_rss";
    ops.start = srss_tasklet_start;
    ops.stop = srss_tasklet_stop;
    ops.handler = srss_tasklet_handler;
    srss->tasklet = mt_sch_register_tasklet(sch, &ops);
    if (!srss->tasklet) {
      err("%s, mt_sch_register_tasklet fail\n", __func__);
      mt_srss_uinit(impl);
      return -EIO;
    }

    rte_atomic32_set(&srss->stop_thread, 0);
    ret = srss_traffic_thread_start(srss);
    if (ret < 0) {
      err("%s(%d), srss_traffic_thread_start fail\n", __func__, i);
      mt_srss_uinit(impl);
      return ret;
    }

    mt_stat_register(impl, srss_stat, srss, "srss");

    info("%s(%d), succ with shared rss mode\n", __func__, i);
  }

  return 0;
}

int mt_srss_uinit(struct mtl_main_impl* impl) {
  int num_ports = mt_num_ports(impl);

  for (int i = 0; i < num_ports; i++) {
    struct mt_srss_impl* srss = impl->srss[i];
    if (!srss) continue;

    mt_stat_unregister(impl, srss_stat, srss);
    srss_traffic_thread_stop(srss);
    if (srss->tasklet) {
      mt_sch_unregister_tasklet(srss->tasklet);
      srss->tasklet = NULL;
    }
    if (srss->sch) {
      mt_sch_put(srss->sch, 0);
      srss->sch = NULL;
    }
    struct mt_srss_entry* entry;
    while ((entry = MT_TAILQ_FIRST(&srss->head))) {
      warn("%s, still has entry %p\n", __func__, entry);
      MT_TAILQ_REMOVE(&srss->head, entry, next);
      mt_rte_free(entry);
    }

    srss_uinit_xdp(srss);

    mt_rte_free(srss);
    impl->srss[i] = NULL;
  }

  return 0;
}