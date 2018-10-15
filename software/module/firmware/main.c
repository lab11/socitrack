#include <string.h>

#include "stm32f0xx_tim.h"
#include "stm32f0xx_pwr.h"
#include "stm32f0xx_usart.h"
#include "stm32f0xx_gpio.h"
#include "stm32f0xx_rcc.h"

#include "board.h"
#include "led.h"

#include "host_interface.h"
#include "dw1000.h"
#include "app_standard_common.h"
#include "app_standard_init.h"
#include "app_standard_resp.h"
#include "app_test_common.h"
#include "app_test_init.h"
#include "app_test_resp.h"
#include "timer.h"
#include "delay.h"
#include "firmware.h"
#include "SEGGER_RTT.h"
#include "SEGGER_RTT_Conf.h"

/******************************************************************************/
// OS state
/******************************************************************************/

// Array of interrupt sources. When an interrupt fires, this array gets marked
// noting that the interrupt fired. The main thread then processes this array
// to get all of the functions it should call.
bool interrupts_triggered[NUMBER_INTERRUPT_SOURCES]  = {FALSE};


/******************************************************************************/
// Current application state
/******************************************************************************/
// Keep track of if the application is active or not
static app_state_e _state = APPSTATE_NOT_INITED;

// Keep track of what application we are running
static module_application_e _current_app;

// Timer for doing periodic operations (like TAG ranging events)
static stm_timer_t* _app_timer;


void start_dw1000 ();

/******************************************************************************/
// "OS" like functions
/******************************************************************************/

// This gets called from interrupt context.
// TODO: Changing interrupts_triggered should be in an atomic block as it is
//       also read from the main loop on the main thread.
void mark_interrupt (interrupt_source_e src) {
	interrupts_triggered[src] = TRUE;
}

static void error () {
    debug_msg("ERROR\r\n");
	GPIO_WriteBit(STM_GPIO3_PORT, STM_GPIO3_PIN, Bit_SET);
	GPIO_WriteBit(STM_GPIO3_PORT, STM_GPIO3_PIN, Bit_RESET);

#if (BOARD_V == SQUAREPOINT)
    // Turn all LEDs off
    GPIO_SetBits(STM_LED_RED_PORT, STM_LED_RED_PIN | STM_LED_BLUE_PIN | STM_LED_GREEN_PIN);

	// Start blinking RED
	while (1) {
		GPIO_WriteBit(STM_LED_RED_PORT, STM_LED_RED_PIN, Bit_SET);
		mDelay(250);
		GPIO_WriteBit(STM_LED_RED_PORT, STM_LED_RED_PIN, Bit_RESET);
		mDelay(250);
	}
#endif
}

/******************************************************************************/
// Main operation functions called by the host interface
/******************************************************************************/

// Call this to configure this TriPoint as the correct application.
// If this is called while the application is stopped, it will not be
// automatically started.
// If this is called when the app is running, the app will be restarted.
void module_configure_app (module_application_e app, void* app_config) {
	bool resume = FALSE;

	// Check if this application is running.
	if (_state == APPSTATE_RUNNING) {
		// Resume with new settings.
		resume = TRUE;
		// Stop this first
		module_stop();
	}

	// Tell the correct application that it should init()
	_current_app = app;
	switch (_current_app) {
		case APP_STANDARD:
			standard_configure((module_config_t*) app_config, NULL);
			break;
		case APP_RANGETEST:
			rangetest_configure((module_config_t*) app_config, NULL);
			break;
	    case APP_SIMPLETEST:
	        // In contrast to normal applications, this function will never return
	        simpletest_configure((module_config_t*) app_config, NULL);
	        break;

		default:
			break;
	}

	// We were running when this function was called, so we start things back
	// up here.
	if (resume) {
		module_start();
	}
}


// Start this node! This will run the anchor and tag algorithms.
void module_start () {
	// Don't start if we are already started
	if (_state == APPSTATE_RUNNING) {
		return;
	}

	_state = APPSTATE_RUNNING;

	switch (_current_app) {
		case APP_STANDARD:
			standard_start();
			break;
		case APP_RANGETEST:
			rangetest_start();

		default:
			break;
	}
}

// This is called when the host tells us to sleep
void module_stop () {
	// Don't stop if we are already stopped
	if (_state == APPSTATE_STOPPED) {
		return;
	}

	_state = APPSTATE_STOPPED;

	switch (_current_app) {
		case APP_STANDARD:
			standard_stop();
			break;

		default:
			break;
	}
}

// Drop the big hammer on the DW1000 and reset the chip (along with the app).
// All state should be preserved, so after the reset the tripoint should go
// back to what it was doing, just after a reset and re-init of the dw1000.
void module_reset () {
	bool resume = FALSE;

	if (_state == APPSTATE_RUNNING) {
		resume = TRUE;
	}

	// Put the app back in the not inited state
	_state = APPSTATE_NOT_INITED;

	// Stop the timer in case it was in use.
	//timer_stop(_app_timer);

	// Init the dw1000, and loop until it works.
	// start does a reset.
	start_dw1000();

	// Re init the app
	switch (_current_app) {
		case APP_STANDARD:
			standard_reset();
			break;

		default:
			break;
	}

	if (resume) {
		module_start();
	}
}

// Return true if we are good for app_configure
// to be called, false otherwise.
bool module_ready () {
	return _state != APPSTATE_NOT_INITED;
}

// Assuming we are a TAG, and we are in on-demand ranging mode, tell
// the dw1000 algorithm to perform a range.
void module_tag_do_range () {
	// If the application isn't running, we are not a tag, or we are not
	// in on-demand ranging mode, don't do anything.
	if (_state != APPSTATE_RUNNING) {
		return;
	}

	// Call the relevant function based on the current application
	switch (_current_app) {
		case APP_STANDARD:
			standard_do_range();
			break;

		case APP_CALIBRATION:
			// Not a thing for this app
			break;

		default:
			break;
	}
}


/******************************************************************************/
// Connection for the anchor/tag code to talk to the main applications
/******************************************************************************/





/******************************************************************************/
// Main
/******************************************************************************/

// Loop until the DW1000 is inited and ready to go.
// TODO: this really shouldn't be blocking as it messes with the I2C interrupt
//       code.
void start_dw1000 () {
	uint32_t err;

	while (1) {
		// Do some preliminary setup of the DW1000. This mostly configures
		// pins and hardware peripherals, as well as straightening out some
		// of the settings on the DW1000.
		uint8_t tries = 0;
		do {
			err = dw1000_init();
			if (err) {
				uDelay(10000);
				tries++;
			}

			// Try to wake up, assuming that DW1000 is sleeping
            if (err && (tries > DW1000_NUM_CONTACT_TRIES_BEFORE_RESET/2)) {
				debug_msg("Rise and shine, sunny boy...\r\n");
				dw1000_force_wakeup();
            }

		} while (err && tries <= DW1000_NUM_CONTACT_TRIES_BEFORE_RESET);

		if (err) {
			// We never got the DW1000 to respond. This puts us in a really
			// bad spot. Maybe if we just wait for a while things will get
			// better?
            debug_msg("ERROR: DW1000 does not respond!\r\n");
			mDelay(50000);

			// Reset DW
			dw1000_reset();
		} else {
			// Success
			break;
		}
	}

	// Successfully started the DW1000
	_state = APPSTATE_STOPPED;
}


int main () {
	uint32_t err;
	bool interrupt_triggered = FALSE;

	// Enable PWR APB clock
	// Not entirely sure why.
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure_A;
    RCC_AHBPeriphClockCmd(STM_GPIO2_CLK, ENABLE);
    GPIO_InitStructure_A.GPIO_Pin 	= STM_GPIO2_PIN;
    GPIO_InitStructure_A.GPIO_Mode 	= GPIO_Mode_OUT;
    GPIO_InitStructure_A.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure_A.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure_A.GPIO_PuPd 	= GPIO_PuPd_NOPULL;
    GPIO_Init(STM_GPIO2_PORT, &GPIO_InitStructure_A);

	GPIO_InitTypeDef GPIO_InitStructure_B;
	RCC_AHBPeriphClockCmd(STM_GPIO3_CLK, ENABLE);
#if (BOARD_V == SQUAREPOINT)
	GPIO_InitStructure_B.GPIO_Pin 	= STM_GPIO3_PIN | STM_LED_RED_PIN | STM_LED_BLUE_PIN | STM_LED_GREEN_PIN;
#elif (BOARD_V == TRIPOINT)
    GPIO_InitStructure_B.GPIO_Pin 	= STM_GPIO3_PIN;
#endif
	GPIO_InitStructure_B.GPIO_Mode 	= GPIO_Mode_OUT;
	GPIO_InitStructure_B.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure_B.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure_B.GPIO_PuPd 	= GPIO_PuPd_NOPULL;
	GPIO_Init(STM_GPIO3_PORT, &GPIO_InitStructure_B);

#if (BOARD_V == SQUAREPOINT)
    // Signal init by turning on WHITE
	GPIO_WriteBit(STM_LED_RED_PORT,   STM_LED_RED_PIN,   Bit_RESET);
    GPIO_WriteBit(STM_LED_BLUE_PORT,  STM_LED_BLUE_PIN,  Bit_RESET);
    GPIO_WriteBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN, Bit_RESET);
#endif

	//Initialize UART1 on GPIO0 and GPIO1
    // Tx: GPIO1 -> Pin 27 -> PB6
    // Rx: GPIO4 -> Pin 28 -> PB7
    /*USART_InitTypeDef usartConfig;
    GPIO_InitTypeDef gpioConfig;

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOB, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);

    GPIO_PinAFConfig(STM_GPIO0_PORT, STM_GPIO0_SRC, GPIO_AF_0);
    GPIO_PinAFConfig(STM_GPIO1_PORT, STM_GPIO1_SRC, GPIO_AF_0);

    gpioConfig.GPIO_Pin 	= STM_GPIO0_PIN | STM_GPIO1_PIN;
    gpioConfig.GPIO_Speed 	= GPIO_Speed_50MHz;
    gpioConfig.GPIO_Mode 	= GPIO_Mode_AF;
    gpioConfig.GPIO_OType 	= GPIO_OType_PP;
    gpioConfig.GPIO_PuPd 	= GPIO_PuPd_UP;
    GPIO_Init(STM_GPIO0_PORT, &gpioConfig);

    // STM "baud" defn wrong; this results in 3 MBaud effective
    usartConfig.USART_BaudRate 	 = 1500000;
    usartConfig.USART_WordLength = USART_WordLength_8b;
    usartConfig.USART_StopBits 	 = USART_StopBits_1;
    usartConfig.USART_Parity 	 = USART_Parity_No;
    usartConfig.USART_Mode 	 	 = USART_Mode_Rx | USART_Mode_Tx;
    usartConfig.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(USART1, &usartConfig);

    USART_Cmd(USART1, ENABLE);*/

    // In case we need a timer, get one. This is used for things like periodic ranging events.
	//_app_timer = timer_init();

#if (BOARD_V == SQUAREPOINT)
	// Signal that internal setup is finished by setting RED
	GPIO_WriteBit(STM_LED_BLUE_PORT,  STM_LED_BLUE_PIN,  Bit_SET);
    GPIO_WriteBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN, Bit_SET);
#endif

	// Next up do some preliminary setup of the DW1000. This mostly configures
	// pins and hardware peripherals, as well as straightening out some
	// of the settings on the DW1000.
    //debug_msg("Configuring DW1000...\r\n");
	start_dw1000();

#ifdef DEBUG_OUTPUT_RTT
    // Test output channels
    // 1. J-Link RTT - Init is used in combination with SEGGER_RTT_IN_RAM to find the correct RAM segment
    SEGGER_RTT_Init();
    debug_msg("\r\n----------------------------------------------\r\n");
    debug_msg("Initialized RTT...\r\n");
#endif

#ifdef DEBUG_OUTPUT_UART
	// 2. Test UART (does not succeed if done before)
	uart_write_message(19, "Initialized UART\r\n");
#endif

//#define BYPASS_HOST_INTERFACE
#ifndef BYPASS_HOST_INTERFACE
	// Initialize the I2C listener. This is the main interface
	// the host controller (that is using TriPoint for ranging/localization)
	// uses to configure how this module operates.
	err = host_interface_init();
	if (err) error();

	// Need to wait for the host board to tell us what to do.
	err = host_interface_wait();
	if (err) error();

	//debug_msg("Waiting for host...\r\n");

#else
	// DEBUG:
	module_config_t config;

	debug_msg("Configured role as: ");

	// Choose role
	if (0) {
	    config.my_role = APP_ROLE_INIT_NORESP;
	    debug_msg("TAG, ");
	} else {
        config.my_role = APP_ROLE_NOINIT_RESP;
        debug_msg("ANCHOR, ");
    }

	// Choose Glossy role
    if (0 || (config.my_role == APP_ROLE_INIT_NORESP)) {
        config.my_glossy_role = GLOSSY_SLAVE;
        debug_msg("GLOSSY_SLAVE\n");
    } else {
        config.my_glossy_role = GLOSSY_MASTER;
        debug_msg("GLOSSY_MASTER\n");
    }

	config.report_mode = MODULE_REPORT_MODE_RANGES;
	config.update_mode = MODULE_UPDATE_MODE_PERIODIC;
	config.update_rate = 10; // in tenths of herz
	config.sleep_mode  = FALSE; // TRUE: sleep in-between rangings

//#define RANGE_TEST
#ifdef RANGE_TEST
	module_configure_app(APP_RANGETEST, &config);

	// If using APP_SIMPLETEST, also comment out "dw1000_configure_settings" in dw1000_init()
    //module_configure_app(APP_SIMPLETEST, &config);
#else
	module_configure_app(APP_STANDARD, &config);
#endif
	module_start();
#endif

#if (BOARD_V == SQUAREPOINT)
	// Signal normal operation by turning on BLUE
	GPIO_WriteBit(STM_LED_RED_PORT,  STM_LED_RED_PIN,  Bit_SET);
	GPIO_WriteBit(STM_LED_BLUE_PORT, STM_LED_BLUE_PIN, Bit_RESET);
#endif

	// MAIN LOOP
	while (1) {

#ifndef	DEBUG_OUTPUT_UART
		PWR_EnterSleepMode(PWR_SLEEPEntry_WFI);
		// PWR_EnterSTOPMode(PWR_Regulator_LowPower, PWR_STOPEntry_WFI);
#endif

		// FIXME: Find reason for delay / code
		// If this does not occur, the interrupt handler does not have time to set the interrupt bit and it will not be handled, as the test is negative for all cases

        GPIO_WriteBit(STM_GPIO2_PORT, STM_GPIO2_PIN, Bit_SET);
		GPIO_WriteBit(STM_GPIO2_PORT, STM_GPIO2_PIN, Bit_RESET);
		//debug_msg("Interrupt triggered\n");

		// When an interrupt fires we end up here.
		// Check all of the interrupt "queues" and call the appropriate
		// callbacks for all of the interrupts that have fired.
		// Do this in a loop in case we get an interrupt during the
		// checks.
		do {
			interrupt_triggered = FALSE;

			if (interrupts_triggered[INTERRUPT_TIMER_17] == TRUE) {
			    // Glossy Timer
			    //debug_msg("Interrupt: TIMER_17 (Glossy)\r\n");
				interrupts_triggered[INTERRUPT_TIMER_17] = FALSE;
				interrupt_triggered = TRUE;
				timer_17_fired();
			}

			if (interrupts_triggered[INTERRUPT_TIMER_16] == TRUE) {
			    // Tag/Anchor timer for SurePoint ranging
			    //debug_msg("Interrupt: TIMER_16 (Tag/Anchor)\r\n");
				interrupts_triggered[INTERRUPT_TIMER_16] = FALSE;
				interrupt_triggered = TRUE;
				timer_16_fired();
			}

			if (interrupts_triggered[INTERRUPT_DW1000] == TRUE) {
			    //debug_msg("Interrupt: DW1000\r\n");
				interrupts_triggered[INTERRUPT_DW1000] = FALSE;
				interrupt_triggered = TRUE;
				dw1000_interrupt_fired();
			}

			if (interrupts_triggered[INTERRUPT_I2C_RX] == TRUE) {
			    //debug_msg("Interrupt: I2C_RX\r\n");
				interrupts_triggered[INTERRUPT_I2C_RX] = FALSE;
				interrupt_triggered = TRUE;
				host_interface_rx_fired();
			}

			if (interrupts_triggered[INTERRUPT_I2C_TX] == TRUE) {
			    //debug_msg("Interrupt: I2C_TX\r\n");
				interrupts_triggered[INTERRUPT_I2C_TX] = FALSE;
				interrupt_triggered = TRUE;
				host_interface_tx_fired();
			}

			if (interrupts_triggered[INTERRUPT_I2C_TIMEOUT] == TRUE) {
			    debug_msg("Interrupt: I2C_TIMEOUT\r\n");
				interrupts_triggered[INTERRUPT_I2C_TIMEOUT] = FALSE;
				interrupt_triggered = TRUE;
				host_interface_timeout_fired();
			}
		} while (interrupt_triggered == TRUE);
	}

	return 0;
}
