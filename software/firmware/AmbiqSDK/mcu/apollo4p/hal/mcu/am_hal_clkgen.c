// ****************************************************************************
//
//! @file am_hal_clkgen.c
//!
//! @brief Functions for interfacing with the CLKGEN.
//!
//! @addtogroup clkgen4_4p CLKGEN - Clock Generator
//! @ingroup apollo4p_hal
//! @{
//
// ****************************************************************************

// ****************************************************************************
//
// Copyright (c) 2024, Ambiq Micro, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// Third party software included in this distribution is subject to the
// additional license terms as defined in the /docs/licenses directory.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// This is part of revision release_sdk_4_5_0-a1ef3b89f9 of the AmbiqSuite Development Package.
//
// ****************************************************************************

#include "am_mcu_apollo.h"

#define CLKGEN_HF2ADJ1_TRIM_OFF_BASE_MASK (CLKGEN_HF2ADJ1_HF2ADJTRIMOFFSET_Msk >> CLKGEN_HF2ADJ1_HF2ADJTRIMOFFSET_Pos)
#define CLKGEN_HF2VAL_TRIMOUT_MASK        (CLKGEN_HF2VAL_HF2ADJTRIMOUT_Msk >> CLKGEN_HF2VAL_HF2ADJTRIMOUT_Pos)

//
//! The max HF2ADJ, used to compute the divider so the max freq is <= this value
//
#define HF2ADJ_MAX_MHZ 12000000

//
//! Apollo4p parts with this or later revisions, don't need a patch
//! to access the HFRC2 tune register
//
#define VALID_TUNE_TRIM_REV 6

//
//! enum used for state of HFRC2-tune register availability.
//
typedef enum
{
    TRIM_ADJUST_VALIDATE_NOT_RUN,           ///!< default/startup state
    TRIM_ADJUST_VALIDATE_NOT_PERMITTED,     ///!< HFRC2-TUNE register not accessible
    TRIM_ADJUST_VALIDATE_IS_PERMITTED,      ///!< HFRC2-TUNE register is accessible
}
trim_adjust_enabled_e;

//
//! settings for CLKGEN->HF2ADJ1_b.HF2ADJTRIMEN
//
#define CLKGEN_HF2ADJ1_HF2ADJTRIMEN_TRIM_OUT    CLKGEN_HF2ADJ1_HF2ADJTRIMEN_TRIM_EN1
#define CLKGEN_HF2ADJ1_HF2ADJTRIMEN_TRIM_OFFSET CLKGEN_HF2ADJ1_HF2ADJTRIMEN_TRIM_EN2
#define CLKGEN_HF2ADJ1_HF2ADJTRIMEN_HF2TUNE     CLKGEN_HF2ADJ1_HF2ADJTRIMEN_TRIM_EN4

#define CLKGEN_HF2ADJ1_HF2ADJTRIMEN_MAX        (CLKGEN_HF2ADJ1_HF2ADJTRIMEN_TRIM_OUT | \
                                                CLKGEN_HF2ADJ1_HF2ADJTRIMEN_TRIM_OFFSET | \
                                                CLKGEN_HF2ADJ1_HF2ADJTRIMEN_HF2TUNE)

//
//! This is the default struct used to control the HFRC2-adjust resample
//! function
//
const am_hal_clockgen_hf2adj_recompute_t tReComputeCtrlDefault =
{
    .ui32NumSamples       = 8,
    //
    //! this will run both the HFRC2-adjust start and the sample average
    //! in this mode it is recommended that there is at last 1000 usec delay
    //! between the HFRC2-adj start and sampling
    //
    .eHF2RecomputeControl = AM_HAL_CLKGEN_HFRC2ADJ_START_AND_SAMPLE,
    //
    //! delay between samples, this should be at least 1
    //
    .ui32SampleDelay_us   = 1,
    //
    //! The delay between HFRC2-adjust start and sampling
    //! this should be at least 1000 usec
    //!
    .ui32StartupDelay_us  = 1000,
    //
    //! The delay between XTLHS (32Mhz) restart and HFRC2 restart
    //! this should be at least tbd usec
    //!
    .ui32Post32MhzRestartDelay_us  = 300,

    //
    //! this will disable the 32Mhz clock when done sampling
    .bDisableXTALHS       = false,
};

//
//! This is the alternate default struct used to control the HFRC2-adjust
//! resample function. This will turn off the 32Mhz clock between re-sampling
//
const am_hal_clockgen_hf2adj_recompute_t tReComputeCtrlNo32MhzDefault =
{
    .ui32NumSamples       = 8,
    //
    //! this will run both the HFRC2-adjust start and the sample average
    //! in this mode it is recommended that there is at last 1000 usec delay
    //! between the HFRC2-adj start and sampling
    //
    .eHF2RecomputeControl = AM_HAL_CLKGEN_HFRC2ADJ_START_AND_SAMPLE,
    //
    //! delay between samples, this should be at least 1
    //
    .ui32SampleDelay_us   = 1,
    //
    //! The delay between XTLHS (32Mhz) restart and HFRC2 restart
    //! this should be at least tbd usec
    //!
    .ui32Post32MhzRestartDelay_us  = 300,

    //
    //! The delay between HFRC2-adjust start and sampling
    //! this should be at least 1000 usec
    //!
    .ui32StartupDelay_us  = 1000,
    //
    //! this will disable the 32Mhz clock when done sampling
    .bDisableXTALHS       = true,
};

//*****************************************************************************
// static function prototypes
//*****************************************************************************

static uint32_t
clkgen_hfrc2_compute_trim_setting (const am_hal_clockgen_hf2adj_compute_t *psHf2Cfg);

static uint32_t
clkgen_sample_and_ave_HF2VAL(uint32_t ui32NumSamples,
                             uint32_t ui32UsecDelay);

static bool
clkgen_is_trim_rev_valid(uint32_t *pui32Status);

//*****************************************************************************
// static functions
//*****************************************************************************

//*****************************************************************************
//
//! @brief this will sample and average the HFRC2-adjust output
//!
//! @param ui32NumSamples  - number of samples
//! @param ui32UsecDelay   - inter-sample delay time in micro-seconds.
//!
//! @return   - an 11 bit number. This number is signed, but is treated
//!             as unsigned on output
//
//*****************************************************************************
static uint32_t
clkgen_sample_and_ave_HF2VAL(uint32_t ui32NumSamples, uint32_t ui32UsecDelay)
{
    // check trim rev
    int32_t i32Ave = 0;
    uint32_t ui32Out = 0;

    if (ui32NumSamples)
    {
        for (uint32_t i = 0; i < ui32NumSamples; i++)
        {
            //
            // Read mux summed output value.
            // This is an 11-bit signed value
            //
            int32_t i32SampleVal = CLKGEN->HF2VAL;

            //
            // Sign-extend for averaging and rounding
            //
            if (i32SampleVal & (1 << 10))
            {
                // this is a negative number sign extend
                i32SampleVal |= ~CLKGEN_HF2VAL_TRIMOUT_MASK;
            }

            i32Ave += i32SampleVal;

            am_hal_delay_us(ui32UsecDelay);
        }

        //
        // Round and average
        //
        if (i32Ave < 0)
        {
            ui32Out = (uint32_t) ((i32Ave - (ui32NumSamples / 2)) / ui32NumSamples);
        }
        else
        {
            ui32Out = (uint32_t) ((i32Ave + (ui32NumSamples / 2)) / ui32NumSamples);
        }

        //
        // Remove the sign extension bits if the value is negative
        // Convert back to an 11-bit signed value
        //
        ui32Out &= CLKGEN_HF2VAL_TRIMOUT_MASK;

    }

    return ui32Out;
} // clkgen_sample_and_ave_HF2VAL()

//*****************************************************************************
//
//! @brief Compute settings for HF2ADJ1 register.
//!
//! @note This updates the HFRC2 trim and output source selection
//!
//! @details This checks the inputs for validity and chooses the best settings.
//!          For backward compatibility will choose default settings
//!
//! @param psHf2Cfg const am_hal_clockgen_hf2adj_compute_t Input params.
//!
//! @return trim setting value, for the HF2ADJ1 register
//
//*****************************************************************************
static uint32_t
clkgen_hfrc2_compute_trim_setting(const am_hal_clockgen_hf2adj_compute_t *psHf2Cfg)
{
    //
    // set up the HF2ADJ1 register values
    //
    uint32_t ui32trimENVal     = (uint32_t) (psHf2Cfg->ui8TrimSetting);
    uint32_t ui32TrimOffsetVal = (psHf2Cfg->ui32TrimValue);

    if ((ui32trimENVal & CLKGEN_HF2ADJ1_HF2ADJTRIMEN_TRIM_OUT) != CLKGEN_HF2ADJ1_HF2ADJTRIMEN_TRIM_OUT)
    {
        //
        // invalid value: FLL output not selected,
        // use default value of all output sources enabled
        //
        ui32trimENVal = (CLKGEN_HF2ADJ1_HF2ADJTRIMEN_TRIM_OUT
                         | CLKGEN_HF2ADJ1_HF2ADJTRIMEN_TRIM_OFFSET
                         | CLKGEN_HF2ADJ1_HF2ADJTRIMEN_HF2TUNE);

        ui32TrimOffsetVal = 0;
    }

    if (ui32TrimOffsetVal > CLKGEN_HF2ADJ1_TRIM_OFF_BASE_MASK)
    {
        //
        // this value is invalid, so set to zero
        //
        ui32TrimOffsetVal = 0;
    }

    uint32_t HJ2ADJ_reg_val = _VAL2FLD(CLKGEN_HF2ADJ1_HF2ADJTRIMEN, ui32trimENVal) |
                              _VAL2FLD(CLKGEN_HF2ADJ1_HF2ADJTRIMOFFSET, ui32TrimOffsetVal);

    return HJ2ADJ_reg_val;

} // clkgen_hfrc2_compute_trim_setting()

//*****************************************************************************
//
//! @brief check for valid trim rev
//!
//! @details If trim rev >= VALID_TUNE_TRIM_REV, the HFRC2 tune register is
//!          always available otherwise the patch register must be checked to
//!          determine if the micro has been patched for the tune register
//!
//! @param pui32Status  pointer to error status to return custom module error codes
//!
//! @return true if tune register is available for use
//
//*****************************************************************************
static bool
clkgen_is_trim_rev_valid(uint32_t *pui32Status)
{
    static trim_adjust_enabled_e eTrimAdjustPermitted = TRIM_ADJUST_VALIDATE_NOT_RUN;

    uint32_t ui32FunctionStatus = AM_HAL_STATUS_SUCCESS;

    if (eTrimAdjustPermitted == TRIM_ADJUST_VALIDATE_NOT_RUN)
    {
        //
        // Need to read trim rev and possibly patch status.
        // To begin, set hfrc2-tune register avail status to invalid.
        // It will be set to valid below if this micro is configured for this operation.
        // That is: tune register is accessible.
        //
        eTrimAdjustPermitted = TRIM_ADJUST_VALIDATE_NOT_PERMITTED;

        uint32_t ui32TrimRevVer;
        uint32_t ui32Ret = am_hal_mram_info_read(1, AM_REG_INFO1_TRIM_REV_O / 4, 1, &ui32TrimRevVer);
        if (AM_HAL_STATUS_SUCCESS == ui32Ret)
        {
            if ((ui32TrimRevVer >= VALID_TUNE_TRIM_REV))
            {
                //
                // Micro is factory configured
                //
                eTrimAdjustPermitted = TRIM_ADJUST_VALIDATE_IS_PERMITTED;
            }
            else
            {
                //
                // The rev is less than 6: check if the patch has been applied
                //
                uint32_t ui32Patch0;
                ui32Ret = am_hal_mram_info_read(1, AM_REG_INFO1_PATCH_TRACKER0_O / 4, 1, &ui32Patch0);
                //
                // Bit three is clear in patch0 indicates the HFRC2-tune register is available
                //
                if (AM_HAL_STATUS_SUCCESS != ui32Ret)
                {
                    ui32FunctionStatus = AM_HAL_CLKGEN_STAT_PATCH_RD_FAIL;
                }
                else if (!(ui32Patch0 & (1 << 3)))
                {
                     eTrimAdjustPermitted = TRIM_ADJUST_VALIDATE_IS_PERMITTED;
                }
                else
                {
                    ui32FunctionStatus = AM_HAL_CLKGEN_STAT_NOT_PATCHED;
                }
            } // ui32TrimRevVer >= 6
        } // AM_HAL_STATUS_SUCCESS == ui32Ret
        else
        {
            ui32FunctionStatus = AM_HAL_CLKGEN_STAT_REV_RD_FAIL;
        }
    } // eTrimAdjustPermitted == TRIM_ADJUST_VALIDATE_NOT_RUN

    //
    //! return the function status
    //
    *pui32Status = ui32FunctionStatus;

    return (eTrimAdjustPermitted == TRIM_ADJUST_VALIDATE_IS_PERMITTED);
}

//*****************************************************************************
//
// @brief Setup HFRC2 adjust based on input values
//
// @param psHf2Cfg  am_hal_clockgen_hf2adj_compute_t containing configuration settings
//
// @return standard hal status
//
//*****************************************************************************
uint32_t
am_hal_hfrc2_adj_control(const am_hal_clockgen_hf2adj_compute_t *psHf2Cfg)
{

    uint32_t ui32ReturnStatus = AM_HAL_STATUS_SUCCESS;
    //
    // Choose xref divider so base freq is <= 8Mhz(ideal) with 12Mhz(Maximum).
    // The target (output) speed is multiplied by 16 to get the
    //   intermediate freq.
    //
    //   Example: 24Mhz output wanted from 32Mhz XTALHS input.
    //
    //   * With the input clock of 32Mhz, want a 24 mhz output.
    //   * Compute the intermediate freq based on 24mhz.
    //      Intermediate value: 24Mhz * 16 = 384Mhz
    //   * Need to divide this to ensure the freq is under 12Mhz,
    //      with 8Mhz is preferred.
    //   * Compute a divider 32Mhz / 12Mhz = 2.6,
    //      The ideal divider is the next value larger: which is 4.
    //   * Divide by 2 and 1 are not supported, so choose at least 4 in any case.
    //
    //   * Therefore, the Input clock is 32Mhz / 4 = 8Mhz base clock input
    //   * 384Mhz/8Mhz = 48 so the multiplier is 48, the register value is
    //      the computed multiplier(48) multiplied by 2^15.
    //   * Then the register value is: 48 * 32768 = 0x180000.
    //

#ifndef AM_HAL_DISABLE_API_VALIDATION
    if (psHf2Cfg == 0)
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }
#endif
    switch (psHf2Cfg->eHF2AdjType)
    {
        case AM_HAL_CLKGEN_HF2ADJ_COMP_COMP_FREQ:
        {
            //
            // compute the HF2ADJ FLL values from the given input frequency and
            // desired output frequency
            //

            //
            // this is the register divider value, what register setting selects the divider below
            //
            uint32_t ui32DivRegVal;

            CLKGEN->HF2ADJ0_b.HF2ADJEN = CLKGEN_HF2ADJ0_HF2ADJEN_DIS;

            CLKGEN->HF2ADJ1 = clkgen_hfrc2_compute_trim_setting(psHf2Cfg);

            //
            // the input frequency
            //
            uint32_t ui32InputFreq = psHf2Cfg->ui32Source_freq_in_hz;

            //
            // compute input divider
            // the clockgen FLL requires an input less than 12Mhz
            // the modules provides dividers of 1,2,4,8 to reduce the input freq
            // to something less than or equal to 12Mhz, less than 8Mhz is preferred.
            //
            // subtract 1 to reduce even dividers (8.00,4.00) to (7.999 and 3.999)
            //
            uint32_t ui32Divider = (ui32InputFreq - 1) / HF2ADJ_MAX_MHZ;

            //
            // Example: a with an input of 32Mhz then:
            //  * The Max ax FLL input freq is 12Mhz.
            //  * ui32Divider = 2(2.666) = (32,000,000 - 1)/12,000,000
            //  * most common case will be between 2 and 4
            //  * The hardware restricts dividers to values of : 8,4,2,1
            //      but 2 and 1 are not supported in hardware
            //  * A computed divider using the equation above >= 8 is invalid
            //
            //  * On the extreme end, a 96,000,000 input becomes
            //      ui32Divider = 7(7.9999) = (96,000,000 - 1)/12,000,000 --valid
            //   * Past the extreme end, a 96,000,001 input becomes
            //      ui32Divider = 8(8) = (96,000,001 - 1)/12,000,000 --invalid
            //

            if (ui32Divider < 4)
            {
                //
                // this will catch any actual dividers from <-> 0-4.00000
                // freq inputs between 0-48,000,000 hz
                //
                ui32Divider = 4;
                ui32DivRegVal = 2;
            }
            else if (ui32Divider < 8)
            {
                //
                // this will catch any actual dividers from 4.00001 <-> 8.00000
                // freq inputs between 48,000,001 - 96,000,000 hz
                //
                //
                ui32Divider = 8;
                ui32DivRegVal = 3;
            }
            else
            {
                //
                // the xtal freq is too high
                //
                ui32ReturnStatus = AM_HAL_STATUS_OUT_OF_RANGE;
                break;
            }

            CLKGEN->HF2ADJ2_b.HF2ADJXTALDIVRATIO = ui32DivRegVal;  // divide the input clock by 4 or 8

            //
            // compute the freq used internally
            // 16 mhz is the divide down from the IF to the final value for the USB input
            // apply final ui64Scaling before ui32Divider to enhance precision
            //
            uint64_t ui64Scaling = (16 * 32768);
            uint64_t ui64_HFRC2FreqScaled =
                (uint64_t) psHf2Cfg->ui32Target_freq_in_hz * ui64Scaling * ui32Divider;
            //
            // round and divide
            //
            uint64_t ui64RegisterValue = ((ui64_HFRC2FreqScaled + (uint64_t) (ui32InputFreq / 2)) /
                                          ui32InputFreq);
            if (ui64RegisterValue > 0x1FFFFFFFull)
            {
                ui32ReturnStatus = AM_HAL_STATUS_OUT_OF_RANGE;
                break;
            }
            CLKGEN->HF2ADJ2_b.HF2ADJRATIO = (uint32_t) ui64RegisterValue;

            if (psHf2Cfg->bDoNotEnableHFRC2 == false)
            {
                CLKGEN->HF2ADJ0_b.HF2ADJEN = CLKGEN_HF2ADJ0_HF2ADJEN_EN;
            }

        } // AM_HAL_CLKGEN_CONTROL_HF2ADJ_COMPUTE sub-case

            break;

        case AM_HAL_CLKGEN_HF2ADJ_COMP_DIRECT_ARG:
            //
            // this is not yet supported
            //
            ui32ReturnStatus = AM_HAL_STATUS_INVALID_ARG;
            break;

        default:
            ui32ReturnStatus = AM_HAL_STATUS_INVALID_ARG;
            break;
    } // switch ( psHf2Cfg->type)

    return ui32ReturnStatus;

} // am_hal_hfrc2_adj_control() : AM_HAL_CLKGEN_CONTROL_HF2ADJ_COMPUTE case

//*****************************************************************************
//
// Sample the HFRC2-adjust output and use as a fixed output.
//
// This is intended to reduce jitter from HFRC2 adjust.
//
//*****************************************************************************
uint32_t
am_hal_clkgen_HFRC2_adj_recompute(const am_hal_clockgen_hf2adj_recompute_t *psRecomputeCtrl)
{

    static uint32_t ui32LastHfrc2Trim = 0;

#ifndef AM_HAL_DISABLE_API_VALIDATION
    if (psRecomputeCtrl == NULL)
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }
#endif
    //
    // This variable contains bits that indicate if this code will do:
    //  * a recompute computation
    //  * a recompute adjust setting
    //  * both
    // It was designed this way to allow the user to insert a delay
    //   (and do something useful during that delay)between the two steps
    //
    am_hal_clockgen_hf2adj_compute_e eSampleControl = psRecomputeCtrl->eHF2RecomputeControl;

    if (eSampleControl & AM_HAL_CLKGEN_HFRC2ADJ_RECOMPUTE_START)
    {
        if (psRecomputeCtrl->bDisableXTALHS)
        {
            am_hal_mcuctrl_control(AM_HAL_MCUCTRL_CONTROL_EXTCLK32M_NORMAL, (void *)&psRecomputeCtrl->mcu_ctrl_xths_controlArg);
            if (psRecomputeCtrl->ui32Post32MhzRestartDelay_us)
            {
                am_hal_delay_us(psRecomputeCtrl->ui32Post32MhzRestartDelay_us);
            }

        }
        //
        // this will start hfadj2 with trim adjust enabled
        //
        CLKGEN->HF2ADJ1            = (CLKGEN_HF2ADJ1_HF2ADJTRIMEN_TRIM_OUT |
                                      CLKGEN_HF2ADJ1_HF2ADJTRIMEN_HF2TUNE);
        CLKGEN->HF2ADJ0_b.HF2ADJEN = CLKGEN_HF2ADJ0_HF2ADJEN_EN;
    }

    if (eSampleControl & AM_HAL_CLKGEN_HFRC2ADJ_RECOMPUTE_SAMPLE)
    {
        uint32_t ui32TrimCheckStatus;
        if (!clkgen_is_trim_rev_valid(&ui32TrimCheckStatus))
        {
            return ui32TrimCheckStatus;
        }

        //
        // Wait for HFRC2 adjust to settle down.
        //
        am_hal_delay_us(psRecomputeCtrl->ui32StartupDelay_us);
        //
        // This returns is an 11-bit signed number.
        // (Treated as unsigned in this function)
        //
        uint32_t ui32Hfrc2Trim = clkgen_sample_and_ave_HF2VAL(
            psRecomputeCtrl->ui32NumSamples,
            psRecomputeCtrl->ui32SampleDelay_us);

        if (ui32LastHfrc2Trim != ui32Hfrc2Trim)
        {
            ui32LastHfrc2Trim = ui32Hfrc2Trim;
            //
            // Put averaged hfrc2-adjust PI output value into the tune register
            // then shut off the HFRC2-adjust, which will cause the tune register
            // to control the HFRC2 frequency. This is a read-modify-write, so
            // use a critical section.
            //
            AM_CRITICAL_BEGIN
                MCUCTRL->HFRC2_b.HF2TUNE = ui32Hfrc2Trim;
            AM_CRITICAL_END
            //
            // Disable HFRC2-adjust
            //
            CLKGEN->HF2ADJ0_b.HF2ADJEN = CLKGEN_HF2ADJ0_HF2ADJEN_DIS;

        }
        else
        {
            //
            // Disable HFRC2-adjust
            //
            CLKGEN->HF2ADJ0_b.HF2ADJEN = CLKGEN_HF2ADJ0_HF2ADJEN_DIS;


        }
        if (psRecomputeCtrl->bDisableXTALHS)
        {
            //
            // Disable the 32Mhz clock
            //
            am_hal_mcuctrl_control(AM_HAL_MCUCTRL_CONTROL_EXTCLK32M_DISABLE, (void *)&psRecomputeCtrl->mcu_ctrl_xths_controlArg);
        }
    }

    return AM_HAL_STATUS_SUCCESS;
} // am_hal_clkgen_HFRC2_adj_recompute()

// ****************************************************************************
//
//  am_hal_clkgen_control()
//      Apply various specific commands/controls on the CLKGEN module.
//
// ****************************************************************************
uint32_t
am_hal_clkgen_control(am_hal_clkgen_control_e eControl, void *pArgs)
{
    uint32_t ui32Regval;
    uint32_t ui32ReturnStatus = AM_HAL_STATUS_SUCCESS;

    switch (eControl)
    {
        case AM_HAL_CLKGEN_CONTROL_RTC_SEL_LFRC:
            CLKGEN->OCTRL_b.OSEL = CLKGEN_OCTRL_OSEL_RTC_LFRC;
            break;

        case AM_HAL_CLKGEN_CONTROL_RTC_SEL_XTAL:
            CLKGEN->OCTRL_b.OSEL = CLKGEN_OCTRL_OSEL_RTC_XT;
            break;

        case AM_HAL_CLKGEN_CONTROL_HFADJ_ENABLE:
            if (pArgs == 0)
            {
                ui32Regval =
                    _VAL2FLD(CLKGEN_HFADJ_HFADJGAIN, CLKGEN_HFADJ_HFADJGAIN_Gain_of_1_in_32) |   /* Slowest attack possible */
                    _VAL2FLD(CLKGEN_HFADJ_HFWARMUP, CLKGEN_HFADJ_HFWARMUP_1SEC)             |   /* Default value */
                    _VAL2FLD(CLKGEN_HFADJ_HFXTADJ, 0x5B8)                                   |   /* Default value */
                    _VAL2FLD(CLKGEN_HFADJ_HFADJCK, CLKGEN_HFADJ_HFADJCK_4SEC)               |   /* Default value */
                    _VAL2FLD(CLKGEN_HFADJ_HFADJEN, CLKGEN_HFADJ_HFADJEN_EN);
            }
            else
            {
                ui32Regval = *(uint32_t*)pArgs;
            }

            //
            // Make sure the ENABLE bit is set.
            //
            ui32Regval |= _VAL2FLD(CLKGEN_HFADJ_HFADJEN, CLKGEN_HFADJ_HFADJEN_EN);
            CLKGEN->HFADJ = ui32Regval;
            break;

        case AM_HAL_CLKGEN_CONTROL_HFADJ_DISABLE:
            CLKGEN->HFADJ_b.HFADJEN = CLKGEN_HFADJ_HFADJEN_DIS;
            break;

        case AM_HAL_CLKGEN_CONTROL_HF2ADJ_ENABLE:
            //
            // set HF2ADJ for 24.576MHz output
            //

            CLKGEN->HF2ADJ0_b.HF2ADJEN = CLKGEN_HF2ADJ0_HF2ADJEN_DIS;

            CLKGEN->HF2ADJ1_b.HF2ADJTRIMEN        = (CLKGEN_HF2ADJ1_HF2ADJTRIMEN_TRIM_OUT   |
                                                     CLKGEN_HF2ADJ1_HF2ADJTRIMEN_HF2TUNE);
            CLKGEN->HF2ADJ2_b.HF2ADJXTALDIVRATIO  = 2;

            //
            // 32MHz XTALHS: default or *pArgs = 0.
            // 24MHz XTALHS: *pArgs is not zero.
            //
            if ( pArgs == 0 || *((uint32_t*)pArgs) == 0)
            {
                CLKGEN->HF2ADJ2_b.HF2ADJRATIO  = 0x189374;  // 24.576Mhz
            }
            else
            {
                CLKGEN->HF2ADJ2_b.HF2ADJRATIO  = 0x200000;
            }

            CLKGEN->HF2ADJ0_b.HF2ADJEN = CLKGEN_HF2ADJ0_HF2ADJEN_EN;
            break;

        case AM_HAL_CLKGEN_CONTROL_HF2ADJ_DISABLE:
            CLKGEN->HF2ADJ0_b.HF2ADJEN = CLKGEN_HF2ADJ0_HF2ADJEN_DIS;
            break;

        case AM_HAL_CLKGEN_CONTROL_HFRC2_START:
            if (CLKGEN->MISC_b.FRCHFRC2 != CLKGEN_MISC_FRCHFRC2_FRC)
            {
                CLKGEN->MISC_b.FRCHFRC2 = CLKGEN_MISC_FRCHFRC2_FRC;

                //
                // Slight delay per the PG's "Clock Switching Procedure"
                //
                am_hal_delay_us(10);
            }
            break;

        case AM_HAL_CLKGEN_CONTROL_HFRC2_STOP:
            if (CLKGEN->MISC_b.FRCHFRC2 != CLKGEN_MISC_FRCHFRC2_NOFRC )
            {
                CLKGEN->MISC_b.FRCHFRC2 = CLKGEN_MISC_FRCHFRC2_NOFRC;
            }
            break;

        case AM_HAL_CLKGEN_CONTROL_DCCLK_ENABLE:
            CLKGEN->DISPCLKCTRL_b.DCCLKEN = 1;
            break;

        case AM_HAL_CLKGEN_CONTROL_DCCLK_DISABLE:
            CLKGEN->DISPCLKCTRL_b.DCCLKEN = 0;
            break;

        case AM_HAL_CLKGEN_CONTROL_DISPCLKSEL_OFF:
            CLKGEN->DISPCLKCTRL_b.DISPCLKSEL = 0;
            break;

        case AM_HAL_CLKGEN_CONTROL_DISPCLKSEL_HFRC48:
            CLKGEN->DISPCLKCTRL_b.DISPCLKSEL = 1;
            break;

        case AM_HAL_CLKGEN_CONTROL_DISPCLKSEL_HFRC96:
            CLKGEN->DISPCLKCTRL_b.DISPCLKSEL = 2;
            break;

        case AM_HAL_CLKGEN_CONTROL_DISPCLKSEL_DPHYPLL:
            CLKGEN->DISPCLKCTRL_b.DISPCLKSEL = 3;
            break;

        case AM_HAL_CLKGEN_CONTROL_PLLCLK_ENABLE:
            CLKGEN->DISPCLKCTRL_b.PLLCLKEN = 1;
            break;

        case AM_HAL_CLKGEN_CONTROL_PLLCLK_DISABLE:
            CLKGEN->DISPCLKCTRL_b.PLLCLKEN = 0;
            break;

        case AM_HAL_CLKGEN_CONTROL_PLLCLKSEL_OFF:
            CLKGEN->DISPCLKCTRL_b.PLLCLKSEL = 0;
            break;

        case AM_HAL_CLKGEN_CONTROL_PLLCLKSEL_HFRC12:
            CLKGEN->DISPCLKCTRL_b.PLLCLKSEL = 1;
            break;

        case AM_HAL_CLKGEN_CONTROL_PLLCLKSEL_HFRC6:
            CLKGEN->DISPCLKCTRL_b.PLLCLKSEL = 2;
            break;

        case AM_HAL_CLKGEN_CONTROL_PLLCLKSEL_HFXT:
            CLKGEN->DISPCLKCTRL_b.PLLCLKSEL = 3;
            break;

        case AM_HAL_CLKGEN_CONTROL_HF2ADJ_COMPUTE:
            ui32ReturnStatus = am_hal_hfrc2_adj_control( (const am_hal_clockgen_hf2adj_compute_t *) pArgs);
            break;

        default:
            ui32ReturnStatus = AM_HAL_STATUS_INVALID_ARG;
    } // switch

    //
    // Return status.
    //
    return ui32ReturnStatus;

} // am_hal_clkgen_control()

// ****************************************************************************
//
//  am_hal_clkgen_status_get()
//  This function returns the current value of various CLKGEN statuses.
//
// ****************************************************************************
uint32_t
am_hal_clkgen_status_get(am_hal_clkgen_status_t *psStatus)
{
    if ( psStatus == NULL )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

    psStatus->ui32SysclkFreq = AM_HAL_CLKGEN_FREQ_MAX_HZ;

    psStatus->eRTCOSC = AM_HAL_CLKGEN_STATUS_RTCOSC_LFRC;
    psStatus->bXtalFailure = false;

    return AM_HAL_STATUS_SUCCESS;

} // am_hal_clkgen_status_get()

// ****************************************************************************
//
//  am_hal_clkgen_clkout_enable()
//  This function is used to select and enable CLKOUT.
//
// ****************************************************************************
uint32_t
am_hal_clkgen_clkout_enable(bool bEnable, am_hal_clkgen_clkout_e eClkSelect)
{
    if ( !bEnable )
    {
        CLKGEN->CLKOUT_b.CKEN = 0;
    }

    //
    // Do a basic validation of the eClkSelect parameter.
    // Not every value in the range is valid, but at least this simple check
    //  provides a reasonable chance that the parameter is valid.
    //
    if ( eClkSelect <= (am_hal_clkgen_clkout_e)AM_HAL_CLKGEN_CLKOUT_MAX )
    {
        //
        // Are we actually changing the frequency?
        //
        if ( CLKGEN->CLKOUT_b.CKSEL != eClkSelect )
        {
            //
            // Disable before changing the clock
            //
            CLKGEN->CLKOUT_b.CKEN = CLKGEN_CLKOUT_CKEN_DIS;

            //
            // Set the new clock select
            //
            CLKGEN->CLKOUT_b.CKSEL = eClkSelect;
        }

        //
        // Enable/disable as requested.
        //
        CLKGEN->CLKOUT_b.CKEN = bEnable ? CLKGEN_CLKOUT_CKEN_EN : CLKGEN_CLKOUT_CKEN_DIS;
    }
    else
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

    //
    // Return success status.
    //
    return AM_HAL_STATUS_SUCCESS;

} // am_hal_clkgen_clkout_enable()


//*****************************************************************************
//
// End Doxygen group.
//! @}
//
//*****************************************************************************
