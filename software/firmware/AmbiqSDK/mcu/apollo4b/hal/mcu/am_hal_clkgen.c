// ****************************************************************************
//
//! @file am_hal_clkgen.c
//!
//! @brief Functions for interfacing with the CLKGEN.
//!
//! @addtogroup clkgen4_4b CLKGEN - Clock Generator
//! @ingroup apollo4b_hal
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

//
//! The max HF2ADJ, used to compute the divider so the max freq is <= this value
//
#define HF2ADJ_MAX_MHZ 12000000

//
//! settings for CLKGEN->HF2ADJ1_b.HF2ADJTRIMEN
//
#define CLKGEN_HF2ADJ1_HF2ADJTRIMEN_TRIM_OUT    CLKGEN_HF2ADJ1_HF2ADJTRIMEN_TRIM_EN1
#define CLKGEN_HF2ADJ1_HF2ADJTRIMEN_TRIM_OFFSET CLKGEN_HF2ADJ1_HF2ADJTRIMEN_TRIM_EN2
#define CLKGEN_HF2ADJ1_HF2ADJTRIMEN_HF2TUNE     CLKGEN_HF2ADJ1_HF2ADJTRIMEN_TRIM_EN4

#define CLKGEN_HF2ADJ1_HF2ADJTRIMEN_MAX        (CLKGEN_HF2ADJ1_HF2ADJTRIMEN_TRIM_OUT | \
                                                CLKGEN_HF2ADJ1_HF2ADJTRIMEN_TRIM_OFFSET | \
                                                CLKGEN_HF2ADJ1_HF2ADJTRIMEN_HF2TUNE)

//*****************************************************************************
// static function prototypes
//*****************************************************************************

static uint32_t
clkgen_hfrc2_compute_trim_setting (const am_hal_clockgen_hf2adj_compute_t *psHf2Cfg);

//*****************************************************************************
// static functions
//*****************************************************************************

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

} // am_hal_hfrc2_adj_control(), AM_HAL_CLKGEN_CONTROL_HF2ADJ_COMPUTE case

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
            //
            //! ERR126: CLKGEN: HFADJ enabled with no modules powered causes incorrect clock generation
            //!
            //! When no peripheral is enabled, HFADJEN should not be enabled.
            //! When used with peripheral disable, there is no need to restore.
            //!
            //! Please see am_hal_pwrctrl.c for further implementation
            //
            if ( PWRCTRL->DEVPWRSTATUS == 0 )
            {
                return AM_HAL_STATUS_FAIL;
            }

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

            //
            //! ERR126: CLKGEN: HFADJ enabled with no modules powered causes incorrect clock generation
            //!
            //! When no peripheral is enabled, HFADJEN should not be enabled.
            //! Delay is required for worst case settling time.
            //
            break;

        case AM_HAL_CLKGEN_CONTROL_HF2ADJ_ENABLE:
            //
            // set HF2ADJ for 24.576MHz output
            //

            CLKGEN->HF2ADJ0_b.HF2ADJEN = CLKGEN_HF2ADJ0_HF2ADJEN_DIS;

            CLKGEN->HF2ADJ1_b.HF2ADJTRIMEN        = (CLKGEN_HF2ADJ1_HF2ADJTRIMEN_TRIM_OUT   |
                                                     CLKGEN_HF2ADJ1_HF2ADJTRIMEN_HF2TUNE    |
                                                     CLKGEN_HF2ADJ1_HF2ADJTRIMEN_TRIM_OFFSET);
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
