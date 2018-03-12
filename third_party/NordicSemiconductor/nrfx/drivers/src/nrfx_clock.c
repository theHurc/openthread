/**
 * Copyright (c) 2016 - 2018, Nordic Semiconductor ASA
 * 
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 * 
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 * 
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 * 
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 * 
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 */

#include <nrfx.h>

#if NRFX_CHECK(NRFX_CLOCK_ENABLED)

#include <nrfx_clock.h>

#define NRFX_LOG_MODULE CLOCK
#include <nrfx_log.h>

#if NRFX_CHECK(NRFX_POWER_ENABLED)
extern bool nrfx_power_irq_enabled;
#endif

#define EVT_TO_STR(event)                                                     \
    (event == NRF_CLOCK_EVENT_HFCLKSTARTED ? "NRF_CLOCK_EVENT_HFCLKSTARTED" : \
    (event == NRF_CLOCK_EVENT_LFCLKSTARTED ? "NRF_CLOCK_EVENT_LFCLKSTARTED" : \
    (event == NRF_CLOCK_EVENT_DONE         ? "NRF_CLOCK_EVENT_DONE"         : \
    (event == NRF_CLOCK_EVENT_CTTO         ? "NRF_CLOCK_EVENT_CTTO"         : \
                                             "UNKNOWN EVENT"))))


/*lint -save -e652 */
#define NRF_CLOCK_LFCLK_RC    CLOCK_LFCLKSRC_SRC_RC
#define NRF_CLOCK_LFCLK_Xtal  CLOCK_LFCLKSRC_SRC_Xtal
#define NRF_CLOCK_LFCLK_Synth CLOCK_LFCLKSRC_SRC_Synth
/*lint -restore */

#if (NRFX_CLOCK_CONFIG_LF_SRC == NRF_CLOCK_LFCLK_RC)
#define CALIBRATION_SUPPORT 1
#else
#define CALIBRATION_SUPPORT 0
#endif

typedef enum
{
    CAL_STATE_IDLE,
    CAL_STATE_CAL
} nrfx_clock_cal_state_t;

/**@brief CLOCK control block. */
typedef struct
{
    nrfx_clock_event_handler_t      event_handler;
    bool                            module_initialized; /*< Indicate the state of module */
#if CALIBRATION_SUPPORT
    volatile nrfx_clock_cal_state_t cal_state;
#endif // CALIBRATION_SUPPORT
} nrfx_clock_cb_t;

static nrfx_clock_cb_t m_clock_cb;

/**
 * This variable is used to check whether common POWER_CLOCK common interrupt
 * should be disabled or not if @ref nrfx_power tries to disable the interrupt.
 */
#if NRFX_CHECK(NRFX_POWER_ENABLED)
bool nrfx_clock_irq_enabled;
#endif

#ifdef NRF52832_XXAA

// ANOMALY 132 - LFCLK needs to avoid frame from 66us to 138us after LFCLK stop. This solution
//               applies delay of 138us before starting LFCLK.
#define ANOMALY_132_REQ_DELAY_US 138UL

// nRF52832 is clocked with 64MHz.
#define ANOMALY_132_NRF52832_FREQ_MHZ 64UL

// Convert time to cycles.
#define ANOMALY_132_DELAY_CYCLES (ANOMALY_132_REQ_DELAY_US * ANOMALY_132_NRF52832_FREQ_MHZ)

/**
 * @brief Function for applying delay of 138us before starting LFCLK.
 */
static void nrfx_clock_anomaly_132(void)
{
    uint32_t cyccnt_inital;
    uint32_t core_debug;
    uint32_t dwt_ctrl;

    // Preserve DEMCR register to do not influence into its configuration. Enable the trace and
    // debug blocks. It is required to read and write data to DWT block.
    core_debug = CoreDebug->DEMCR;
    CoreDebug->DEMCR = core_debug | CoreDebug_DEMCR_TRCENA_Msk;

    // Preserve CTRL register in DWT block to do not influence into its configuration. Make sure
    // that cycle counter is enabled.
    dwt_ctrl = DWT->CTRL;
    DWT->CTRL = dwt_ctrl | DWT_CTRL_CYCCNTENA_Msk;

    // Store start value of cycle counter.
    cyccnt_inital = DWT->CYCCNT;

    // Delay required time.
    while ((DWT->CYCCNT - cyccnt_inital) < ANOMALY_132_DELAY_CYCLES)
    {}

    // Restore preserved registers.
    DWT->CTRL = dwt_ctrl;
    CoreDebug->DEMCR = core_debug;
}

#endif // NRF52832_XXAA

nrfx_err_t nrfx_clock_init(nrfx_clock_event_handler_t event_handler)
{
    NRFX_ASSERT(event_handler);

    nrfx_err_t err_code = NRFX_SUCCESS;
    if (m_clock_cb.module_initialized)
    {
        err_code = NRFX_ERROR_ALREADY_INITIALIZED;
    }
    else
    {
#if CALIBRATION_SUPPORT
        m_clock_cb.cal_state = CAL_STATE_IDLE;
#endif
        m_clock_cb.event_handler = event_handler;
        m_clock_cb.module_initialized = true;
    }

    NRFX_LOG_INFO("Function: %s, error code: %s.", __func__, NRFX_LOG_ERROR_STRING_GET(err_code));
    return err_code;
}

void nrfx_clock_enable(void)
{
    NRFX_ASSERT(m_clock_cb.module_initialized);
    nrfx_power_clock_irq_init();
    nrf_clock_lf_src_set((nrf_clock_lfclk_t)NRFX_CLOCK_CONFIG_LF_SRC);

#if NRFX_CHECK(NRFX_POWER_ENABLED)
    nrfx_clock_irq_enabled = true;
#endif

    NRFX_LOG_INFO("Module enabled.");
}

void nrfx_clock_disable(void)
{
    NRFX_ASSERT(m_clock_cb.module_initialized);
#if NRFX_CHECK(NRFX_POWER_ENABLED)
    NRFX_ASSERT(nrfx_clock_irq_enabled);
    if (!nrfx_power_irq_enabled)
#endif
    {
        NRFX_IRQ_DISABLE(POWER_CLOCK_IRQn);
    }
    nrf_clock_int_disable(CLOCK_INTENSET_HFCLKSTARTED_Msk |
                          CLOCK_INTENSET_LFCLKSTARTED_Msk |
                          CLOCK_INTENSET_DONE_Msk |
                          CLOCK_INTENSET_CTTO_Msk);
#if NRFX_CHECK(NRFX_POWER_ENABLED)
    nrfx_clock_irq_enabled = false;
#endif
    NRFX_LOG_INFO("Module disabled.");
}

void nrfx_clock_uninit(void)
{
    NRFX_ASSERT(m_clock_cb.module_initialized);
    nrfx_clock_lfclk_stop();
    nrfx_clock_hfclk_stop();
    m_clock_cb.module_initialized = false;
    NRFX_LOG_INFO("Uninitialized.");
}

void nrfx_clock_lfclk_start(void)
{
    NRFX_ASSERT(m_clock_cb.module_initialized);
    nrf_clock_event_clear(NRF_CLOCK_EVENT_LFCLKSTARTED);
    nrf_clock_int_enable(NRF_CLOCK_INT_LF_STARTED_MASK);

#ifdef NRF52832_XXAA
    nrfx_clock_anomaly_132();
#endif

    nrf_clock_task_trigger(NRF_CLOCK_TASK_LFCLKSTART);
}

void nrfx_clock_lfclk_stop(void)
{
    NRFX_ASSERT(m_clock_cb.module_initialized);
    nrf_clock_task_trigger(NRF_CLOCK_TASK_LFCLKSTOP);
    while (nrf_clock_lf_is_running())
    {}
}

void nrfx_clock_hfclk_start(void)
{
    NRFX_ASSERT(m_clock_cb.module_initialized);
    nrf_clock_event_clear(NRF_CLOCK_EVENT_HFCLKSTARTED);
    nrf_clock_int_enable(NRF_CLOCK_INT_HF_STARTED_MASK);
    nrf_clock_task_trigger(NRF_CLOCK_TASK_HFCLKSTART);
}

void nrfx_clock_hfclk_stop(void)
{
    NRFX_ASSERT(m_clock_cb.module_initialized);
    nrf_clock_task_trigger(NRF_CLOCK_TASK_HFCLKSTOP);
    while (nrf_clock_hf_is_running(NRF_CLOCK_HFCLK_HIGH_ACCURACY))
    {}
}

nrfx_err_t nrfx_clock_calibration_start(void)
{
    nrfx_err_t err_code = NRFX_SUCCESS;
#if CALIBRATION_SUPPORT
    if (nrfx_clock_hfclk_is_running() == false)
    {
        return NRFX_ERROR_INVALID_STATE;
    }

    if (nrfx_clock_lfclk_is_running() == false)
    {
        return NRFX_ERROR_INVALID_STATE;
    }

    if (m_clock_cb.cal_state == CAL_STATE_IDLE)
    {
        nrf_clock_event_clear(NRF_CLOCK_EVENT_DONE);
        nrf_clock_int_enable(NRF_CLOCK_INT_DONE_MASK);
        m_clock_cb.cal_state = CAL_STATE_CAL;
        nrf_clock_task_trigger(NRF_CLOCK_TASK_CAL);
    }
    else
    {
        err_code = NRFX_ERROR_BUSY;
    }
#endif // CALIBRATION_SUPPORT
    NRFX_LOG_WARNING("Function: %s, error code: %s.",
                     __func__,
                     NRFX_LOG_ERROR_STRING_GET(err_code));
    return err_code;
}

nrfx_err_t nrfx_clock_is_calibrating(void)
{
#if CALIBRATION_SUPPORT
    if (m_clock_cb.cal_state == CAL_STATE_CAL)
    {
        return NRFX_ERROR_BUSY;
    }
#endif
    return NRFX_SUCCESS;
}

void nrfx_clock_calibration_timer_start(uint8_t interval)
{
    nrf_clock_cal_timer_timeout_set(interval);
    nrf_clock_event_clear(NRF_CLOCK_EVENT_CTTO);
    nrf_clock_int_enable(NRF_CLOCK_INT_CTTO_MASK);
    nrf_clock_task_trigger(NRF_CLOCK_TASK_CTSTART);
}

void nrfx_clock_calibration_timer_stop(void)
{
    nrf_clock_int_disable(NRF_CLOCK_INT_CTTO_MASK);
    nrf_clock_task_trigger(NRF_CLOCK_TASK_CTSTOP);
}

void nrfx_clock_irq_handler(void)
{
    if (nrf_clock_event_check(NRF_CLOCK_EVENT_HFCLKSTARTED))
    {
        nrf_clock_event_clear(NRF_CLOCK_EVENT_HFCLKSTARTED);
        NRFX_LOG_DEBUG("Event: %s.", EVT_TO_STR(NRF_CLOCK_EVENT_HFCLKSTARTED));
        nrf_clock_int_disable(NRF_CLOCK_INT_HF_STARTED_MASK);

        m_clock_cb.event_handler(NRFX_CLOCK_EVT_HFCLK_STARTED);
    }
    if (nrf_clock_event_check(NRF_CLOCK_EVENT_LFCLKSTARTED))
    {
        nrf_clock_event_clear(NRF_CLOCK_EVENT_LFCLKSTARTED);
        NRFX_LOG_DEBUG("Event: %s.", EVT_TO_STR(NRF_CLOCK_EVENT_LFCLKSTARTED));
        nrf_clock_int_disable(NRF_CLOCK_INT_LF_STARTED_MASK);

        m_clock_cb.event_handler(NRFX_CLOCK_EVT_LFCLK_STARTED);
    }
#if CALIBRATION_SUPPORT
    if (nrf_clock_event_check(NRF_CLOCK_EVENT_CTTO))
    {
        nrf_clock_event_clear(NRF_CLOCK_EVENT_CTTO);
        NRFX_LOG_DEBUG("Event: %s.", EVT_TO_STR(NRF_CLOCK_EVENT_CTTO));
        nrf_clock_int_disable(NRF_CLOCK_INT_CTTO_MASK);

        m_clock_cb.event_handler(NRFX_CLOCK_EVT_CTTO);
    }

    if (nrf_clock_event_check(NRF_CLOCK_EVENT_DONE))
    {
        nrf_clock_event_clear(NRF_CLOCK_EVENT_DONE);
        NRFX_LOG_DEBUG("Event: %s.", EVT_TO_STR(NRF_CLOCK_EVENT_DONE));
        nrf_clock_int_disable(NRF_CLOCK_INT_DONE_MASK);
        m_clock_cb.cal_state = CAL_STATE_IDLE;
        m_clock_cb.event_handler(NRFX_CLOCK_EVT_CAL_DONE);
    }
#endif // CALIBRATION_SUPPORT
}

#undef NRF_CLOCK_LFCLK_RC
#undef NRF_CLOCK_LFCLK_Xtal
#undef NRF_CLOCK_LFCLK_Synth

#endif // NRFX_CHECK(NRFX_CLOCK_ENABLED)
