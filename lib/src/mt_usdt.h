/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Intel Corporation
 */

#ifndef _MT_LIB_USDT_HEAD_H_
#define _MT_LIB_USDT_HEAD_H_

#ifdef MTL_HAS_USDT

/* mt_usdt_provider.h auto generated by the dtrace tool from mt_usdt_provider.d */
#include "mt_usdt_provider.h"

#define MT_DTRACE_PROBE(provider, probe) DTRACE_PROBE(provider, probe)
#define MT_DTRACE_PROBE1(provider, probe, parm1) DTRACE_PROBE1(provider, probe, parm1)
#define MT_DTRACE_PROBE2(provider, probe, parm1, parm2) \
  DTRACE_PROBE2(provider, probe, parm1, parm2)
#define MT_DTRACE_PROBE3(provider, probe, parm1, parm2, parm3) \
  DTRACE_PROBE3(provider, probe, parm1, parm2, parm3)
#define MT_DTRACE_PROBE4(provider, probe, parm1, parm2, parm3, parm4) \
  DTRACE_PROBE4(provider, probe, parm1, parm2, parm3, parm4)
#define MT_DTRACE_PROBE5(provider, probe, parm1, parm2, parm3, parm4, parm5) \
  DTRACE_PROBE5(provider, probe, parm1, parm2, parm3, parm4, parm5)
#define MT_DTRACE_PROBE6(provider, probe, parm1, parm2, parm3, parm4, parm5, parm6) \
  DTRACE_PROBE6(provider, probe, parm1, parm2, parm3, parm4, parm5, parm6)
#else

#define MT_DTRACE_PROBE(provider, probe) \
  do {                                   \
  } while (0)
#define MT_DTRACE_PROBE1(provider, probe, parm1) \
  do {                                           \
  } while (0)
#define MT_DTRACE_PROBE2(provider, probe, parm1, parm2) \
  do {                                                  \
  } while (0)
#define MT_DTRACE_PROBE3(provider, probe, parm1, parm2, parm3) \
  do {                                                         \
  } while (0)
#define MT_DTRACE_PROBE4(provider, probe, parm1, parm2, parm3, parm4) \
  do {                                                                \
  } while (0)
#define MT_DTRACE_PROBE5(provider, probe, parm1, parm2, parm3, parm4, parm5) \
  do {                                                                       \
  } while (0)
#define MT_DTRACE_PROBE6(provider, probe, parm1, parm2, parm3, parm4, parm5, parm6) \
  do {                                                                              \
  } while (0)

#define SYS_LOG_MSG_ENABLED() (0)

#endif

#define MT_USDT_PTP_MSG(port, stage, value) \
  MT_DTRACE_PROBE3(ptp, ptp_msg, port, stage, value)
#define MT_USDT_PTP_RESULT(port, delta, correct) \
  MT_DTRACE_PROBE3(ptp, ptp_result, port, delta, correct)

#define MT_USDT_SYS_LOG_MSG(level, msg) MT_DTRACE_PROBE2(sys, log_msg, level, msg)

#define MT_USDT_SYS_LOG_MSG_ENABLED() SYS_LOG_MSG_ENABLED()

#define MT_USDT_ST20_TX_FRAME_NEXT(m_idx, s_idx, f_idx, va, tmstamp) \
  MT_DTRACE_PROBE5(st20, tx_frame_next, m_idx, s_idx, f_idx, va, tmstamp)
#define MT_USDT_ST20_TX_FRAME_DONE(m_idx, s_idx, f_idx, tmstamp) \
  MT_DTRACE_PROBE4(st20, tx_frame_done, m_idx, s_idx, f_idx, tmstamp)

#define MT_USDT_ST20_RX_FRAME_AVAILABLE(m_idx, s_idx, f_idx, va, tmstamp, data_size) \
  MT_DTRACE_PROBE6(st20, rx_frame_available, m_idx, s_idx, f_idx, va, tmstamp, data_size)
#define MT_USDT_ST20_RX_FRAME_PUT(m_idx, s_idx, f_idx, va) \
  MT_DTRACE_PROBE4(st20, rx_frame_put, m_idx, s_idx, f_idx, va)
#define MT_USDT_ST20_RX_NO_FRAMEBUFFER(m_idx, s_idx, tmstamp) \
  MT_DTRACE_PROBE3(st20, rx_no_framebuffer, m_idx, s_idx, tmstamp)

#define MT_USDT_ST30_TX_FRAME_NEXT(m_idx, s_idx, f_idx, va) \
  MT_DTRACE_PROBE4(st30, tx_frame_next, m_idx, s_idx, f_idx, va)
#define MT_USDT_ST30_TX_FRAME_DONE(m_idx, s_idx, f_idx, tmstamp) \
  MT_DTRACE_PROBE4(st30, tx_frame_done, m_idx, s_idx, f_idx, tmstamp)

#define MT_USDT_ST30_RX_FRAME_AVAILABLE(m_idx, s_idx, f_idx, va, tmstamp, data_size) \
  MT_DTRACE_PROBE6(st30, rx_frame_available, m_idx, s_idx, f_idx, va, tmstamp, data_size)
#define MT_USDT_ST30_RX_FRAME_PUT(m_idx, s_idx, f_idx, va) \
  MT_DTRACE_PROBE4(st30, rx_frame_put, m_idx, s_idx, f_idx, va)
#define MT_USDT_ST30_RX_NO_FRAMEBUFFER(m_idx, s_idx, tmstamp) \
  MT_DTRACE_PROBE3(st30, rx_no_framebuffer, m_idx, s_idx, tmstamp)

#define MT_USDT_ST40_TX_FRAME_NEXT(m_idx, s_idx, f_idx, va, meta_num, total_udw) \
  MT_DTRACE_PROBE6(st40, tx_frame_next, m_idx, s_idx, f_idx, va, meta_num, total_udw)
#define MT_USDT_ST40_TX_FRAME_DONE(m_idx, s_idx, f_idx, tmstamp) \
  MT_DTRACE_PROBE4(st40, tx_frame_done, m_idx, s_idx, f_idx, tmstamp)
#define MT_USDT_ST40_RX_MBUF_AVAILABLE(m_idx, s_idx, mbuf, tmstamp, data_size) \
  MT_DTRACE_PROBE5(st40, rx_mbuf_available, m_idx, s_idx, mbuf, tmstamp, data_size)
#define MT_USDT_ST40_RX_MBUF_PUT(m_idx, s_idx, mbuf) \
  MT_DTRACE_PROBE3(st40, rx_mbuf_put, m_idx, s_idx, mbuf)
#define MT_USDT_ST40_RX_MBUF_ENQUEUE_FAIL(m_idx, s_idx, mbuf, tmstamp) \
  MT_DTRACE_PROBE4(st40, rx_mbuf_enqueue_fail, m_idx, s_idx, mbuf, tmstamp)

#endif
