/******************************************************************************
* Copyright (C) 2022 Maxim Integrated Products, Inc., All Rights Reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included
* in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
* OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL MAXIM INTEGRATED BE LIABLE FOR ANY CLAIM, DAMAGES
* OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
* OTHER DEALINGS IN THE SOFTWARE.
*
* Except as contained in this notice, the name of Maxim Integrated
* Products, Inc. shall not be used except as stated in the Maxim Integrated
* Products, Inc. Branding Policy.
*
* The mere transfer of this software does not imply any licenses
* of trade secrets, proprietary technology, copyrights, patents,
* trademarks, maskwork rights, or any other form of intellectual
* property whatsoever. Maxim Integrated Products, Inc. retains all
* ownership rights.
* 
******************************************************************************/

/* **** Includes **** */
#include <string.h>
#include "mxc_device.h"
#include "mxc_assert.h"
#include "mxc_pins.h"
#include "mxc_sys.h"
#include "mxc_delay.h"
#include "csi2.h"
#include "csi2_reva.h"
#include "nvic_table.h"
#include "dma.h"
#include "dma_reva.h"
#include "mcr_regs.h"

/* **** Definitions **** */

#define MXC_CSI2_REVA_CTRL_ERRINT_FL                                                 \
    (MXC_F_CSI2_REVA_RX_EINT_CTRL_IE_EECC2 | MXC_F_CSI2_REVA_RX_EINT_CTRL_IE_EECC1 | \
     MXC_F_CSI2_REVA_RX_EINT_CTRL_IE_ECRC | MXC_F_CSI2_REVA_RX_EINT_CTRL_IE_EID)

#define MXC_CSI2_REVA_VFIFO_ERRINT_FL                                                 \
    (MXC_F_CSI2_REVA_RX_EINT_VFF_IF_OUTSYNC | MXC_F_CSI2_REVA_RX_EINT_VFF_IF_FMTERR | \
     MXC_F_CSI2_REVA_RX_EINT_VFF_IF_RAW_AHBERR)

#define MXC_CSI2_REVA_VFIFO_FIFOINT_FL                                              \
    (MXC_F_CSI2_REVA_RX_EINT_VFF_IF_FNEMPTY | MXC_F_CSI2_REVA_RX_EINT_VFF_IF_FTHD | \
     MXC_F_CSI2_REVA_RX_EINT_VFF_IF_FFULL)

#define MXC_CSI2_REVA_VFIFO_FIFOINT_EN                                              \
    (MXC_F_CSI2_REVA_RX_EINT_VFF_IE_FNEMPTY | MXC_F_CSI2_REVA_RX_EINT_VFF_IE_FTHD | \
     MXC_F_CSI2_REVA_RX_EINT_VFF_IE_FFULL)

#define MXC_CSI2_REVA_VFIFO_DETECT_MD                                                   \
    (MXC_F_CSI2_REVA_RX_EINT_VFF_IE_FNEMP_MD | MXC_F_CSI2_REVA_RX_EINT_VFF_IE_FTHD_MD | \
     MXC_F_CSI2_REVA_RX_EINT_VFF_IE_FFUL_MD)

/* **** Globals **** */

static volatile uint8_t *rx_data = NULL;
static volatile uint32_t rx_data_index;
static volatile uint32_t dphy_rdy;
static volatile uint32_t frame_end_cnt;
static volatile uint32_t line_cnt;
static volatile uint32_t odd_line_byte_num;
static volatile uint32_t even_line_byte_num;
static volatile uint32_t line_byte_num;
static volatile uint32_t frame_byte_num;

// Used for Non-DMA CSI2 Cases
static volatile uint32_t bits_per_pixel;
static volatile uint32_t fifo_burst_size;
static volatile mxc_csi2_reva_fifo_trig_t fifo_int_trig;
static volatile mxc_csi2_ahbwait_t ahbwait_en;

typedef struct {
    mxc_csi2_req_t *req;
    mxc_csi2_ctrl_cfg_t *ctrl_cfg;
    mxc_csi2_vfifo_cfg_t *vfifo_cfg;
    int dma_channel;
} csi2_reva_req_state_t;

csi2_reva_req_state_t csi2_state;

/* **** Functions **** */

/******************************************/
/* Global Control/Configuration Functions */
/******************************************/

int MXC_CSI2_RevA_Init(mxc_csi2_reva_regs_t *csi2, mxc_csi2_req_t *req,
                       mxc_csi2_ctrl_cfg_t *ctrl_cfg, mxc_csi2_vfifo_cfg_t *vfifo_cfg)
{
    int error;

    dphy_rdy = 0;
    line_cnt = 0;
    frame_end_cnt = 0;

    csi2_state.req = req;
    csi2_state.ctrl_cfg = ctrl_cfg;
    csi2_state.vfifo_cfg = vfifo_cfg;

    rx_data = (uint8_t *)(req->img_addr);
    rx_data_index = 0;

    // Convert respective pixel bit number to bytes
    frame_byte_num = ((req->bits_per_pixel_odd + req->bits_per_pixel_even) * req->pixels_per_line *
                      req->lines_per_frame) >>
                     4;
    odd_line_byte_num = (req->bits_per_pixel_odd * req->pixels_per_line) >> 3;
    even_line_byte_num = (req->bits_per_pixel_even * req->pixels_per_line) >> 3;

    // Presetting with even line byte number as default
    line_byte_num = even_line_byte_num;

    // D-PHY reset
    csi2->dphy_rst_n = 0x00;

    error = MXC_CSI2_CTRL_Config(ctrl_cfg);
    if (error != E_NO_ERROR) {
        return error;
    }

    error = MXC_DMA_Init();
    if (error != E_NO_ERROR) {
        return error;
    }

    // Configure VFIFO
    csi2->vfifo_pixel_num = req->pixels_per_line;
    csi2->vfifo_line_num = req->lines_per_frame;

    error = MXC_CSI2_VFIFO_Config(vfifo_cfg);
    if (error != E_NO_ERROR) {
        return error;
    }

    error = MXC_CSI2_VFIFO_ProcessRAWtoRGB(req);
    if (error != E_NO_ERROR) {
        return error;
    }

    return E_NO_ERROR;
}

int MXC_CSI2_RevA_Shutdown(mxc_csi2_reva_regs_t *csi2)
{
    int error;

    error = MXC_CSI2_Stop();
    if (error != E_NO_ERROR) {
        return error;
    }

    MXC_CSI2_CTRL_DisableInt(0xFFFFFFFF);
    MXC_CSI2_VFIFO_DisableInt(0xFFFFFFFF);
    MXC_CSI2_PPI_DisableInt(0xFFFFFFFF);

    error = MXC_DMA_ReleaseChannel(csi2_state.dma_channel);
    if (error != E_NO_ERROR) {
        return error;
    }

    return E_NO_ERROR;
}

int MXC_CSI2_RevA_Start(mxc_csi2_reva_regs_t *csi2, int num_data_lanes)
{
    int i;
    int enable_dlanes;

    dphy_rdy = 0;
    line_cnt = 0;
    frame_end_cnt = 0;

    MXC_CSI2_CTRL_ClearFlags(0xFFFFFFFF);
    MXC_CSI2_VFIFO_ClearFlags(0xFFFFFFFF);
    MXC_CSI2_PPI_ClearFlags(0xFFFFFFFF);

    // Enable VFIFO
    MXC_CSI2_VFIFO_Enable();

    // Release DPHY reset
    csi2->dphy_rst_n |= 0x01;

    // Enable CSI2 lanes
    csi2->cfg_clk_lane_en = 0x01;

    // This function assumes configured data lanes are used sequentially (e.g. x2 data lanes = data lane0 and lane1).
    enable_dlanes = 0;
    for (i = 0; i < num_data_lanes; i++) {
        enable_dlanes |= (1 << i);
    }

    csi2->cfg_data_lane_en = enable_dlanes;

    // Must include setting this bit.
    csi2->xcfgi_dw0b = 0x10000000;

    return E_NO_ERROR;
}

int MXC_CSI2_RevA_Stop(mxc_csi2_reva_regs_t *csi2)
{
    int error;

    // Only release channel when DMA was used
    if (csi2_state.vfifo_cfg->dma_mode != MXC_CSI2_DMA_NO_DMA) {
        error = MXC_DMA_ReleaseChannel(csi2_state.dma_channel);
        if (error != E_NO_ERROR) {
            return error;
        }
    }

    // Enable VFIFO
    MXC_CSI2_VFIFO_Disable();

    // Reset DPHY
    csi2->dphy_rst_n = 0x00;

    // Enable CSI2 lanes
    csi2->cfg_clk_lane_en = 0;
    csi2->cfg_data_lane_en = 0;

    return E_NO_ERROR;
}

int MXC_CSI2_RevA_CaptureFrameDMA(int num_data_lanes)
{
    int i;
    int error;
    int dma_byte_cnt;
    int dlane_stop_inten;

    mxc_csi2_req_t *req = csi2_state.req;
    mxc_csi2_vfifo_cfg_t *vfifo = csi2_state.vfifo_cfg;

    // Convert respective pixel bit number to bytes
    frame_byte_num = ((req->bits_per_pixel_odd + req->bits_per_pixel_even) * req->pixels_per_line *
                      req->lines_per_frame) >>
                     4;
    odd_line_byte_num = (req->bits_per_pixel_odd * req->pixels_per_line) >> 3;
    even_line_byte_num = (req->bits_per_pixel_even * req->pixels_per_line) >> 3;

    // Select lower line byte number (Odd line)
    line_byte_num = odd_line_byte_num;

    // Use whole frame vs line by line
    if (vfifo->dma_whole_frame == MXC_CSI2_DMA_WHOLE_FRAME) {
        dma_byte_cnt = frame_byte_num;
    } else {
        // Smaller
        dma_byte_cnt = line_byte_num;
    }

    error = MXC_CSI2_DMA_Config(req->img_addr, dma_byte_cnt, vfifo->rx_thd);
    if (error != E_NO_ERROR) {
        return error;
    }

    // Enable Stop State interrupts for all used data lanes
    dlane_stop_inten = 0;
    for (i = 0; i < num_data_lanes; i++) {
        dlane_stop_inten |= (1 << i);
    }

    dlane_stop_inten <<= MXC_F_CSI2_RX_EINT_PPI_IE_DL0STOP_POS;
    MXC_CSI2_PPI_EnableInt(dlane_stop_inten);

    error = MXC_CSI2_Start(num_data_lanes);
    if (error != E_NO_ERROR) {
        return error;
    }

    return E_NO_ERROR;
}

int MXC_CSI2_RevA_SetLaneCtrlSource(mxc_csi2_reva_regs_t *csi2, mxc_csi2_lane_src_t *src)
{
    if (src == NULL) {
        return E_BAD_PARAM;
    }

    csi2->cfg_d0_swap_sel = src->d0_swap_sel;
    csi2->cfg_d1_swap_sel = src->d1_swap_sel;
    csi2->cfg_d2_swap_sel = src->d2_swap_sel;
    csi2->cfg_d3_swap_sel = src->d3_swap_sel;
    csi2->cfg_c0_swap_sel = src->c0_swap_sel;

    return E_NO_ERROR;
}

int MXC_CSI2_RevA_GetLaneCtrlSource(mxc_csi2_reva_regs_t *csi2, mxc_csi2_lane_src_t *src)
{
    if (src == NULL) {
        return E_BAD_PARAM;
    }

    src->d0_swap_sel = csi2->cfg_d0_swap_sel;
    src->d1_swap_sel = csi2->cfg_d1_swap_sel;
    src->d2_swap_sel = csi2->cfg_d2_swap_sel;
    src->d3_swap_sel = csi2->cfg_d3_swap_sel;
    src->c0_swap_sel = csi2->cfg_c0_swap_sel;

    return E_NO_ERROR;
}

void MXC_CSI2_RevA_GetImageDetails(uint8_t **img, uint32_t *imgLen, uint32_t *w, uint32_t *h)
{
    *img = (uint8_t *)csi2_state.req->img_addr;
    *imgLen = frame_byte_num;

    *w = csi2_state.req->pixels_per_line;
    *h = csi2_state.req->lines_per_frame;
}

int MXC_CSI2_RevA_Callback(mxc_csi2_req_t *req, int retVal)
{
    if (req == NULL) {
        return E_BAD_PARAM;
    }

    if (req->callback != NULL) {
        req->callback(req, retVal);
    }

    return E_NO_ERROR;
}

int MXC_CSI2_RevA_Handler(mxc_csi2_reva_regs_t *csi2)
{
    uint32_t ctrl_flags, vfifo_flags, ppi_flags;
    mxc_csi2_req_t *req = csi2_state.req;

    ctrl_flags = MXC_CSI2_CTRL_GetFlags();
    ppi_flags = MXC_CSI2_PPI_GetFlags();
    vfifo_flags = MXC_CSI2_VFIFO_GetFlags();

    // Clear Flags
    MXC_CSI2_CTRL_ClearFlags(ctrl_flags);
    MXC_CSI2_PPI_ClearFlags(ppi_flags);
    MXC_CSI2_VFIFO_ClearFlags(vfifo_flags);

    // Handle RX CTRL Interrupts
    if (ctrl_flags & MXC_F_CSI2_REVA_RX_EINT_CTRL_IE_PKTFFOV) {
        MXC_CSI2_Callback(req, E_OVERRUN);
        MXC_CSI2_Stop();
        return E_OVERRUN;
    }

    // Check for RX CTRL CRC, ECC, and ID Errors
    if (ctrl_flags & MXC_CSI2_REVA_CTRL_ERRINT_FL) {
        MXC_CSI2_Callback(req, E_COMM_ERR);
        MXC_CSI2_Stop();
        return E_COMM_ERR;
    }

    // Handle PPI if waiting for frame capture
    if (ppi_flags & MXC_F_CSI2_REVA_RX_EINT_PPI_IF_DL0STOP) {
        MXC_CSI2_PPI_Stop();
        MXC_CSI2_Callback(req, E_NO_ERROR);

        MXC_CSI2_PPI_ClearFlags(ppi_flags);

        // Handle VFIFO Flags
    } else {
        // VFIFO Error Checking
        if (vfifo_flags & MXC_F_CSI2_REVA_RX_EINT_VFF_IF_UNDERRUN) {
            MXC_CSI2_Callback(req, E_UNDERRUN);
            MXC_CSI2_VFIFO_Disable(); // Stop VFIFO
            return E_UNDERRUN;
        }

        if (vfifo_flags &
            (MXC_F_CSI2_REVA_RX_EINT_VFF_IF_OVERRUN | MXC_F_CSI2_REVA_RX_EINT_VFF_IF_RAW_OVR)) {
            MXC_CSI2_Callback(req, E_OVERRUN);
            MXC_CSI2_VFIFO_Disable(); // Stop VFIFO
            return E_OVERRUN;
        }

        if (vfifo_flags & MXC_F_CSI2_REVA_RX_EINT_VFF_IF_AHBWTO) {
            MXC_CSI2_Callback(req, E_TIME_OUT);
            MXC_CSI2_VFIFO_Disable(); // Stop VFIFO
            return E_TIME_OUT;
        }

        // Check for Out of Sync, Formatting, or RAW AHB errors.
        if (vfifo_flags & MXC_CSI2_REVA_VFIFO_ERRINT_FL) {
            MXC_CSI2_Callback(req, E_COMM_ERR);
            MXC_CSI2_VFIFO_Disable(); // Stop VFIFO
            return E_COMM_ERR;
        }

        // Check for frame end flag if no errors.
        if (vfifo_flags & MXC_F_CSI2_REVA_RX_EINT_VFF_IF_FE) { // GT
            frame_end_cnt++;
        }
        MXC_CSI2_VFIFO_ClearFlags(vfifo_flags);
    }

    return E_NO_ERROR;
}

/********************************/
/* CSI2 RX Controller Functions */
/********************************/

int MXC_CSI2_RevA_CTRL_Config(mxc_csi2_reva_regs_t *csi2, mxc_csi2_ctrl_cfg_t *cfg)
{
    // Set Power Ready
    csi2->aon_power_ready_n &= ~0x01;

    // Invert RX Clock
    if (cfg->invert_ppi_clk) {
        csi2->rxbyteclkhs_inv = 0x01;
    } else {
        csi2->rxbyteclkhs_inv = 0x00;
    }

    // Setting number of lanes used
    csi2->cfg_num_lanes = cfg->num_lanes;

    // Must select payload data type
    if ((cfg->payload0 == MXC_CSI2_PL0_DISABLE_ALL) &&
        (cfg->payload1 == MXC_CSI2_PL1_DISABLE_ALL)) {
        return E_NOT_SUPPORTED;
    }

    csi2->cfg_disable_payload_0 = cfg->payload0;
    csi2->cfg_disable_payload_1 = cfg->payload1;

    // Set flush count
    if (cfg->flush_cnt < 0 || cfg->flush_cnt > 15) {
        return E_BAD_PARAM;
    }

    csi2->cfg_flush_count = cfg->flush_cnt;

    // Configure Data and Clock Lane Control Source
    MXC_CSI2_SetLaneCtrlSource(&(cfg->lane_src));

    return E_NO_ERROR;
}

void MXC_CSI2_RevA_CTRL_EnableInt(mxc_csi2_reva_regs_t *csi2, uint32_t mask)
{
    // Clear flags before enabling
    csi2->rx_eint_ctrl_if |= mask;

    csi2->rx_eint_ctrl_ie |= mask;
}

void MXC_CSI2_RevA_CTRL_DisableInt(mxc_csi2_reva_regs_t *csi2, uint32_t mask)
{
    csi2->rx_eint_ctrl_ie &= ~mask;
}

int MXC_CSI2_RevA_CTRL_GetFlags(mxc_csi2_reva_regs_t *csi2)
{
    return (csi2->rx_eint_ctrl_if);
}

void MXC_CSI2_RevA_CTRL_ClearFlags(mxc_csi2_reva_regs_t *csi2, uint32_t flags)
{
    csi2->rx_eint_ctrl_if |= flags;
}

/************************/
/* CSI2 VFIFO Functions */
/************************/

int MXC_CSI2_RevA_VFIFO_Config(mxc_csi2_reva_regs_t *csi2, mxc_csi2_vfifo_cfg_t *cfg)
{
    int error;

    csi2->vfifo_cfg1 = (cfg->flow_ctrl) | ((cfg->wait_cyc << MXC_F_CSI2_VFIFO_CFG1_AHBWCYC_POS) &
                                           MXC_F_CSI2_VFIFO_CFG1_AHBWCYC);

    // Set virtual channel
    if (cfg->virtual_channel > 3 || cfg->virtual_channel < 0) {
        return E_BAD_PARAM;
    }

    MXC_SETFIELD(csi2->vfifo_cfg0, MXC_F_CSI2_REVA_VFIFO_CFG0_VC,
                 cfg->virtual_channel << MXC_F_CSI2_REVA_VFIFO_CFG0_VC_POS);

    error = MXC_CSI2_VFIFO_SetDMAMode(cfg->dma_mode);
    if (error != E_NO_ERROR) {
        return error;
    }

    // Enable AHB WAIT
    if (cfg->wait_en) {
        MXC_SETFIELD(csi2->vfifo_cfg0, MXC_F_CSI2_REVA_VFIFO_CFG0_AHBWAIT,
                     0x1 << MXC_F_CSI2_REVA_VFIFO_CFG0_AHBWAIT_POS);
    } else {
        MXC_SETFIELD(csi2->vfifo_cfg0, MXC_F_CSI2_REVA_VFIFO_CFG0_AHBWAIT,
                     0x0 << MXC_F_CSI2_REVA_VFIFO_CFG0_AHBWAIT_POS);
    }

    // Select FIFO Read Mode (One-by-One vs Direct Addressing for each entity)
    if (cfg->fifo_rd_mode) {
        MXC_SETFIELD(csi2->vfifo_cfg0, MXC_F_CSI2_REVA_VFIFO_CFG0_FIFORM,
                     0x1 << MXC_F_CSI2_REVA_VFIFO_CFG0_FIFORM_POS);
    } else {
        MXC_SETFIELD(csi2->vfifo_cfg0, MXC_F_CSI2_REVA_VFIFO_CFG0_FIFORM,
                     0x0 << MXC_F_CSI2_REVA_VFIFO_CFG0_FIFORM_POS);
    }

    // Enable Error Detection
    if (cfg->err_det_en) {
        MXC_SETFIELD(csi2->vfifo_cfg0, MXC_F_CSI2_REVA_VFIFO_CFG0_ERRDE,
                     0x1 << MXC_F_CSI2_REVA_VFIFO_CFG0_ERRDE_POS);
    } else {
        MXC_SETFIELD(csi2->vfifo_cfg0, MXC_F_CSI2_REVA_VFIFO_CFG0_ERRDE,
                     0x0 << MXC_F_CSI2_REVA_VFIFO_CFG0_ERRDE_POS);
    }

    // Select Normal mode or Full Bandwidth mode
    if (cfg->bandwidth_mode) {
        MXC_SETFIELD(csi2->vfifo_cfg0, MXC_F_CSI2_REVA_VFIFO_CFG0_FBWM,
                     0x1 << MXC_F_CSI2_VFIFO_CFG0_FBWM_POS);
    } else {
        MXC_SETFIELD(csi2->vfifo_cfg0, MXC_F_CSI2_REVA_VFIFO_CFG0_FBWM,
                     0x0 << MXC_F_CSI2_VFIFO_CFG0_FBWM_POS);
    }

    // Set RX Threshold
    if (cfg->rx_thd >= MXC_CSI2_FIFO_DEPTH) {
        return E_BAD_PARAM;
    }

    MXC_SETFIELD(csi2->vfifo_ctrl, MXC_F_CSI2_REVA_VFIFO_CTRL_THD,
                 cfg->rx_thd << MXC_F_CSI2_REVA_VFIFO_CTRL_THD_POS);

    return E_NO_ERROR;
}

int MXC_CSI2_RevA_VFIFO_ProcessRAWtoRGB(mxc_csi2_reva_regs_t *csi2, mxc_csi2_req_t *req)
{
    int error;

    csi2->vfifo_raw_buf0_addr = req->raw_buf0_addr;
    csi2->vfifo_raw_buf1_addr = req->raw_buf1_addr;

    // Process RAW to Selected RGB Type if applicable
    if (req->process_raw_to_rgb) {
        error = MXC_CSI2_VFIFO_SetRGBType(req->rgb_type);
        if (error != E_NO_ERROR) {
            return error;
        }

        error = MXC_CSI2_VFIFO_SetRAWFormat(req->raw_format);
        if (error != E_NO_ERROR) {
            return error;
        }

        if (req->autoflush) {
            MXC_SETFIELD(csi2->vfifo_raw_ctrl, MXC_F_CSI2_VFIFO_RAW_CTRL_RAW_FF_AFO,
                         0x1 << MXC_F_CSI2_VFIFO_RAW_CTRL_RAW_FF_AFO_POS);
        } else {
            MXC_SETFIELD(csi2->vfifo_raw_ctrl, MXC_F_CSI2_VFIFO_RAW_CTRL_RAW_FF_AFO,
                         0x0 << MXC_F_CSI2_VFIFO_RAW_CTRL_RAW_FF_AFO_POS);
        }

        csi2->vfifo_raw_ctrl |= MXC_F_CSI2_REVA_VFIFO_RAW_CTRL_RAW_CEN;
    }

    return E_NO_ERROR;
}

int MXC_CSI2_RevA_VFIFO_NextFIFOTrigMode(mxc_csi2_reva_regs_t *csi2, uint8_t ff_not_empty,
                                         uint8_t ff_abv_thd, uint8_t ff_full)
{
    // Disable FIFO-related interrupts and clear FIFO trigger detection mode before switching
    MXC_CSI2_VFIFO_DisableInt(MXC_CSI2_REVA_VFIFO_FIFOINT_EN);
    MXC_CSI2_VFIFO_ChangeIntMode(MXC_CSI2_REVA_VFIFO_FIFOINT_EN, 0);

    // Set to next FIFO trigger if applicable
    switch (fifo_int_trig) {
    case MXC_CSI2_REVA_FF_TRIG_NOT_EMPTY:
        if (ff_abv_thd) {
            fifo_int_trig = MXC_CSI2_REVA_FF_TRIG_ABV_THD;
        } else if (ff_full) {
            fifo_int_trig = MXC_CSI2_REVA_FF_TRIG_FULL;
        } else {
            fifo_int_trig = MXC_CSI2_REVA_FF_TRIG_NOT_EMPTY;
        }
        break;

    case MXC_CSI2_REVA_FF_TRIG_ABV_THD:
        if (ff_full) {
            fifo_int_trig = MXC_CSI2_REVA_FF_TRIG_FULL;
        } else if (ff_not_empty) {
            fifo_int_trig = MXC_CSI2_REVA_FF_TRIG_NOT_EMPTY;
        } else {
            fifo_int_trig = MXC_CSI2_REVA_FF_TRIG_ABV_THD;
        }
        break;

    case MXC_CSI2_REVA_FF_TRIG_FULL:
        if (ff_not_empty) {
            fifo_int_trig = MXC_CSI2_REVA_FF_TRIG_NOT_EMPTY;
        } else if (ff_abv_thd) {
            fifo_int_trig = MXC_CSI2_REVA_FF_TRIG_ABV_THD;
        } else {
            fifo_int_trig = MXC_CSI2_REVA_FF_TRIG_FULL;
        }
        break;

    default:
        fifo_int_trig = MXC_CSI2_REVA_FF_TRIG_NO_TRIGGER;
        break;
    }

    // Set new burst length and ahbwait parameters if applicable
    switch (fifo_int_trig) {
    case MXC_CSI2_REVA_FF_TRIG_NOT_EMPTY:
        fifo_burst_size = bits_per_pixel >> 1;
        ahbwait_en = MXC_CSI2_AHBWAIT_ENABLE;
        break;

    case MXC_CSI2_REVA_FF_TRIG_ABV_THD:
        fifo_burst_size = csi2_state.vfifo_cfg->rx_thd;
        ahbwait_en = MXC_CSI2_AHBWAIT_DISABLE;
        break;

    case MXC_CSI2_REVA_FF_TRIG_FULL:
        fifo_burst_size = 64; // Max burst size
        ahbwait_en = MXC_CSI2_AHBWAIT_DISABLE;
        break;

    default:
        return E_BAD_PARAM;
    }

    // Update new configurations for next FIFO read
    MXC_CSI2_VFIFO_SetAHBWait(ahbwait_en);
    MXC_CSI2_VFIFO_EnableInt(fifo_int_trig, 1);

    return E_NO_ERROR;
}

void MXC_CSI2_RevA_VFIFO_EnableInt(mxc_csi2_reva_regs_t *csi2, uint32_t mask, uint32_t edge)
{
    // Clear flags before enabling
    csi2->rx_eint_vff_if |= mask;

    csi2->rx_eint_vff_ie |= mask;

    // Set edge triggered or level triggered FIFO modes if applicable
    MXC_CSI2_VFIFO_ChangeIntMode(mask, edge);
}

void MXC_CSI2_RevA_VFIFO_ChangeIntMode(mxc_csi2_reva_regs_t *csi2, uint32_t mask, uint32_t edge)
{
    // Edge Triggered Mode
    if (edge && (mask & MXC_CSI2_REVA_VFIFO_FIFOINT_EN)) {
        // Set corresponding detection mode for FIFO not empty, above thd, and full interrupts
        csi2->rx_eint_vff_ie |=
            ((mask & MXC_CSI2_REVA_VFIFO_FIFOINT_EN) << MXC_F_CSI2_RX_EINT_VFF_IE_FNEMP_MD_POS);

        // Level Triggered Mode
    } else if (!edge && (mask & MXC_CSI2_REVA_VFIFO_FIFOINT_EN)) {
        // Clear corresponding detection mode for FIFO not empty, above thd, and full interrupts
        csi2->rx_eint_vff_ie &=
            ~((mask & MXC_CSI2_REVA_VFIFO_FIFOINT_EN) << MXC_F_CSI2_RX_EINT_VFF_IE_FNEMP_MD_POS);
    }
}

void MXC_CSI2_RevA_VFIFO_DisableInt(mxc_csi2_reva_regs_t *csi2, uint32_t mask)
{
    csi2->rx_eint_vff_ie &= ~mask;
}

int MXC_CSI2_RevA_VFIFO_GetFlags(mxc_csi2_reva_regs_t *csi2)
{
    return (csi2->rx_eint_vff_if);
}

void MXC_CSI2_RevA_VFIFO_ClearFlags(mxc_csi2_reva_regs_t *csi2, uint32_t flags)
{
    csi2->rx_eint_vff_if |= flags;
}

int MXC_CSI2_RevA_VFIFO_Enable(mxc_csi2_reva_regs_t *csi2)
{
    csi2->vfifo_ctrl |= MXC_F_CSI2_REVA_VFIFO_CTRL_FIFOEN;

    return E_NO_ERROR;
}

int MXC_CSI2_RevA_VFIFO_Disable(mxc_csi2_reva_regs_t *csi2)
{
    csi2->vfifo_ctrl &= ~MXC_F_CSI2_REVA_VFIFO_CTRL_FIFOEN;

    return E_NO_ERROR;
}

int MXC_CSI2_RevA_VFIFO_SetPayloadType(mxc_csi2_reva_regs_t *csi2, mxc_csi2_payload0_t payload0,
                                       mxc_csi2_payload1_t payload1)
{
    // Need to set one Payload data type
    if ((payload0 == MXC_CSI2_PL0_DISABLE_ALL) && (payload1 == MXC_CSI2_PL1_DISABLE_ALL)) {
        return E_BAD_PARAM;
    }

    csi2->cfg_disable_payload_0 = payload0;
    csi2->cfg_disable_payload_1 = payload1;

    return E_NO_ERROR;
}

int MXC_CSI2_RevA_VFIFO_GetPayloadType(mxc_csi2_reva_regs_t *csi2, uint32_t *payload0,
                                       uint32_t *payload1)
{
    if (payload0 == NULL || payload1 == NULL) {
        return E_NULL_PTR;
    }

    *payload0 = csi2->cfg_disable_payload_0;
    *payload1 = csi2->cfg_disable_payload_1;

    return E_NO_ERROR;
}

int MXC_CSI2_RevA_VFIFO_SetDMAMode(mxc_csi2_reva_regs_t *csi2, mxc_csi2_dma_mode_t dma_mode)
{
    // Check for valid DMA Mode
    if (dma_mode < MXC_CSI2_DMA_NO_DMA || dma_mode > MXC_CSI2_DMA_FIFO_FULL) {
        return E_BAD_PARAM;
    }

    MXC_SETFIELD(csi2->vfifo_cfg0, MXC_F_CSI2_REVA_VFIFO_CFG0_DMAMODE, dma_mode);

    return E_NO_ERROR;
}

mxc_csi2_dma_mode_t MXC_CSI2_RevA_VFIFO_GetDMAMode(mxc_csi2_reva_regs_t *csi2)
{
    int dma_mode;
    mxc_csi2_dma_mode_t result;

    dma_mode = csi2->vfifo_cfg0 & MXC_F_CSI2_REVA_VFIFO_CFG0_DMAMODE;
    switch (dma_mode) {
    // No DMA
    case MXC_S_CSI2_REVA_VFIFO_CFG0_DMAMODE_NO_DMA:
        result = MXC_CSI2_DMA_NO_DMA;
        break;

    // DMA Request
    case MXC_S_CSI2_REVA_VFIFO_CFG0_DMAMODE_DMA_REQ:
        result = MXC_CSI2_DMA_SEND_REQUEST;
        break;

    // FIFO Above Threshold
    case MXC_S_CSI2_REVA_VFIFO_CFG0_DMAMODE_FIFO_THD:
        result = MXC_CSI2_DMA_FIFO_ABV_THD;
        break;

    // FIFO Full
    case MXC_S_CSI2_REVA_VFIFO_CFG0_DMAMODE_FIFO_FULL:
        result = MXC_CSI2_DMA_FIFO_FULL;
        break;

    default:
        return E_BAD_PARAM;
    }

    return result;
}

int MXC_CSI2_RevA_VFIFO_SetRGBType(mxc_csi2_reva_regs_t *csi2, mxc_csi2_rgb_type_t rgb_type)
{
    // Check for valid RGB Type
    if (rgb_type < MXC_CSI2_TYPE_RGB444 || rgb_type > MXC_CSI2_TYPE_RGB888) {
        return E_BAD_PARAM;
    }

    MXC_SETFIELD(csi2->vfifo_raw_ctrl, MXC_F_CSI2_REVA_VFIFO_RAW_CTRL_RGB_TYP, rgb_type);

    return E_NO_ERROR;
}

mxc_csi2_rgb_type_t MXC_CSI2_RevA_VFIFO_GetRGBType(mxc_csi2_reva_regs_t *csi2)
{
    int rgb_type;
    mxc_csi2_rgb_type_t result;

    rgb_type = csi2->vfifo_raw_ctrl & MXC_F_CSI2_REVA_VFIFO_RAW_CTRL_RGB_TYP;

    switch (rgb_type) {
    // RGB444
    case MXC_S_CSI2_REVA_VFIFO_RAW_CTRL_RGB_TYP_RGB444:
        result = MXC_CSI2_TYPE_RGB444;
        break;

    // RGB555
    case MXC_S_CSI2_REVA_VFIFO_RAW_CTRL_RGB_TYP_RGB555:
        result = MXC_CSI2_TYPE_RGB555;
        break;

    // RGB565
    case MXC_S_CSI2_REVA_VFIFO_RAW_CTRL_RGB_TYP_RGB565:
        result = MXC_CSI2_TYPE_RGB565;
        break;

    // RGB666
    case MXC_S_CSI2_REVA_VFIFO_RAW_CTRL_RGB_TYP_RGB666:
        result = MXC_CSI2_TYPE_RGB666;
        break;

    // RGB888
    case MXC_S_CSI2_REVA_VFIFO_RAW_CTRL_RGB_TYP_RGG888:
        result = MXC_CSI2_TYPE_RGB888;
        break;

    default:
        return E_BAD_PARAM;
    }

    return result;
}

int MXC_CSI2_RevA_VFIFO_SetRAWFormat(mxc_csi2_reva_regs_t *csi2, mxc_csi2_raw_format_t raw_format)
{
    // Check for valid format
    if (raw_format < MXC_CSI2_FORMAT_RGRG_GBGB || raw_format > MXC_CSI2_FORMAT_BGBG_GRGR) {
        return E_BAD_PARAM;
    }

    MXC_SETFIELD(csi2->vfifo_raw_ctrl, MXC_F_CSI2_REVA_VFIFO_RAW_CTRL_RAW_FMT, raw_format);

    return E_NO_ERROR;
}

mxc_csi2_raw_format_t MXC_CSI2_RevA_VFIFO_GetRAWFormat(mxc_csi2_reva_regs_t *csi2)
{
    int raw_format;
    mxc_csi2_raw_format_t result;

    raw_format = (csi2->vfifo_raw_ctrl & MXC_F_CSI2_REVA_VFIFO_RAW_CTRL_RAW_FMT) >>
                 MXC_F_CSI2_REVA_VFIFO_RAW_CTRL_RAW_FMT_POS;

    switch (raw_format) {
    // RGRG_GBGB
    case MXC_V_CSI2_REVA_VFIFO_RAW_CTRL_RAW_FMT_RGRG_GBGB:
        result = MXC_CSI2_FORMAT_RGRG_GBGB;
        break;

    // GRGR_BGBG
    case MXC_V_CSI2_REVA_VFIFO_RAW_CTRL_RAW_FMT_GRGR_BGBG:
        result = MXC_CSI2_FORMAT_GRGR_BGBG;
        break;

    // GBGB_RGRG
    case MXC_V_CSI2_REVA_VFIFO_RAW_CTRL_RAW_FMT_GBGB_RGRG:
        result = MXC_CSI2_FORMAT_GBGB_RGRG;
        break;

    // BGBG_GRGR
    case MXC_V_CSI2_REVA_VFIFO_RAW_CTRL_RAW_FMT_BGBG_GRGR:
        result = MXC_CSI2_FORMAT_BGBG_GRGR;
        break;

    default:
        return E_BAD_STATE;
    }

    return result;
}

int MXC_CSI2_RevA_VFIFO_GetFIFOEntityCount(mxc_csi2_reva_regs_t *csi2)
{
    return ((csi2->vfifo_sts & MXC_F_CSI2_VFIFO_STS_FELT) >> MXC_F_CSI2_VFIFO_STS_FELT_POS);
}

void MXC_CSI2_RevA_VFIFO_SetAHBWait(mxc_csi2_reva_regs_t *csi2, mxc_csi2_ahbwait_t wait_en)
{
    // Enable AHB Wait
    if (wait_en) {
        if (csi2_state.vfifo_cfg->wait_en == MXC_CSI2_AHBWAIT_DISABLE) {
            csi2_state.vfifo_cfg->wait_en = MXC_CSI2_AHBWAIT_ENABLE;
        }

        MXC_SETFIELD(csi2->vfifo_cfg0, MXC_F_CSI2_REVA_VFIFO_CFG0_AHBWAIT,
                     (1 << MXC_F_CSI2_REVA_VFIFO_CFG0_AHBWAIT_POS));

        // Disable AHB Wait
    } else {
        if (csi2_state.vfifo_cfg->wait_en == MXC_CSI2_AHBWAIT_ENABLE) {
            csi2_state.vfifo_cfg->wait_en = MXC_CSI2_AHBWAIT_DISABLE;
        }

        MXC_SETFIELD(csi2->vfifo_cfg0, MXC_F_CSI2_REVA_VFIFO_CFG0_AHBWAIT,
                     (0 << MXC_F_CSI2_REVA_VFIFO_CFG0_AHBWAIT_POS));
    }
}

mxc_csi2_ahbwait_t MXC_CSI2_RevA_VFIFO_GetAHBWait(mxc_csi2_reva_regs_t *csi2)
{
    int ahbwait;

    ahbwait = (csi2->vfifo_cfg0 & MXC_F_CSI2_REVA_VFIFO_CFG0_AHBWAIT) >>
              MXC_F_CSI2_REVA_VFIFO_CFG0_AHBWAIT_POS;

    if (ahbwait) {
        return MXC_CSI2_AHBWAIT_ENABLE;
    } else {
        return MXC_CSI2_AHBWAIT_DISABLE;
    }
}

/***********************************************/
/* CSI2 PHY Protocol Interface (PPI) Functions */
/***********************************************/

void MXC_CSI2_RevA_PPI_EnableInt(mxc_csi2_reva_regs_t *csi2, uint32_t mask)
{
    // Clear flags before enabling
    csi2->rx_eint_ppi_if |= mask;

    csi2->rx_eint_ppi_ie |= mask;
}

void MXC_CSI2_RevA_PPI_DisableInt(mxc_csi2_reva_regs_t *csi2, uint32_t mask)
{
    csi2->rx_eint_ppi_ie &= ~mask;
}

int MXC_CSI2_RevA_PPI_GetFlags(mxc_csi2_reva_regs_t *csi2)
{
    return (csi2->rx_eint_ppi_if);
}

void MXC_CSI2_RevA_PPI_ClearFlags(mxc_csi2_reva_regs_t *csi2, uint32_t flags)
{
    csi2->rx_eint_ppi_if |= flags;
}

int MXC_CSI2_RevA_PPI_Stop(void)
{
    MXC_CSI2_PPI_DisableInt(0xFFFFFFFF);

    return E_NO_ERROR;
}

/************************************/
/* CSI2 DMA - Used for all features */
/************************************/

int MXC_CSI2_RevA_DMA_Config(uint8_t *dst_addr, uint32_t byte_cnt, uint32_t burst_size)
{
    int error;
    uint8_t channel;
    mxc_dma_config_t config;
    mxc_dma_srcdst_t srcdst;
    mxc_dma_adv_config_t advConfig = { 0, 0, 0, 0, 0, 0 };

    channel = MXC_DMA_AcquireChannel();
    csi2_state.dma_channel = channel;

    config.reqsel = MXC_DMA_REQUEST_CSI2RX;
    config.ch = channel;
    config.srcwd = MXC_DMA_WIDTH_WORD;
    config.dstwd = MXC_DMA_WIDTH_WORD;
    config.srcinc_en = 0;
    config.dstinc_en = 1;

    advConfig.ch = channel;
    advConfig.burst_size = burst_size;

    srcdst.ch = channel;
    srcdst.source = MXC_CSI2_FIFO;
    srcdst.dest = dst_addr;
    srcdst.len = byte_cnt;

    error = MXC_DMA_ConfigChannel(config, srcdst);
    if (error != E_NO_ERROR) {
        return error;
    }

    error = MXC_DMA_SetCallback(channel, MXC_CSI2_DMA_Callback);
    if (error != E_NO_ERROR) {
        return error;
    }

    error = MXC_DMA_SetChannelInterruptEn(channel, false, true);
    if (error != E_NO_ERROR) {
        return error;
    }

    error = MXC_DMA_AdvConfigChannel(advConfig);
    if (error != E_NO_ERROR) {
        return error;
    }

    error = MXC_DMA_EnableInt(channel);
    if (error != E_NO_ERROR) {
        return error;
    }

    error = MXC_DMA_Start(channel);
    if (error != E_NO_ERROR) {
        return error;
    }

    return E_NO_ERROR;
}

int MXC_CSI2_RevA_DMA_GetChannel(void)
{
    return csi2_state.dma_channel;
}

int MXC_CSI2_RevA_DMA_GetCurrentLineCnt(void)
{
    return line_cnt;
}

int MXC_CSI2_RevA_DMA_GetCurrentFrameEndCnt(void)
{
    return frame_end_cnt;
}

void MXC_CSI2_RevA_DMA_Callback(mxc_dma_reva_regs_t *dma, int a, int b)
{
    mxc_csi2_req_t *req = csi2_state.req;
    uint32_t dma_channel = csi2_state.dma_channel;
    uint32_t dma_whole_frame = csi2_state.vfifo_cfg->dma_whole_frame;

    // Clear CTZ Status Flag
    dma->ch[dma_channel].status |= MXC_F_DMA_STATUS_CTZ_IF;

    // Track frame completion
    if (!dma_whole_frame) {
        // This should be the only place to write line_cnt.
        line_cnt++;
        if (line_cnt >= req->lines_per_frame) {
            line_cnt -= req->lines_per_frame;
            frame_end_cnt++;
        }
    } else {
        line_cnt += csi2_state.req->lines_per_frame;
        frame_end_cnt++;
    }

    // Set DMA Counter
    if (frame_end_cnt < req->frame_num) {
        if (dma_whole_frame) {
            dma->ch[dma_channel].cnt = frame_byte_num;
        } else {
            // Handle image types with different even and odd line
            if (line_cnt & 0x01) {
                dma->ch[dma_channel].cnt = odd_line_byte_num;
            } else {
                dma->ch[dma_channel].cnt = even_line_byte_num;
            }
        }

        // Re-enable DMA Channel
        dma->ch[dma_channel].ctrl |= MXC_F_DMA_REVA_CTRL_EN;
    } else {
        MXC_CSI2_VFIFO_Disable();
    }
}

/**@} end of group csi2 */
