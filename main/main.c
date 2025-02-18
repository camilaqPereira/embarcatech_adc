#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "pico/stdlib.h"

#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "pico/time.h"
#include "pico/bootrom.h"

#include "ssd1306/ssd1306.h"

/* Constants*/

#define DEBOUNCE_TIME 200000        // Debounce time for pushbuttons
#define JOY_CENTER 1990.0           // Joystick resting position (x and y)

#define CENTER_RADIUS 100.0         // Radius to reduce LED avtivation by trepidation 
#define JOY_MIN_ADC 20              // MIN experimental ADC value
#define JOY_MAX_ADC 4087            // MAX experimental ADC value

#define DISPLAY_I2C_SDA 14          // GPIO SSD1306 SDA
#define DISPLAY_I2C_SCL 15          //GPIO SSD1306 SCL
#define DISPLAY_I2C_PORT i2c1       // GPIO SSD1306 PORT
#define DISPLAY_I2C_FREQ 400000     //DISPLAY I2C FREQUENCY
#define DISPLAY_I2C_ADDRESS 0x3c    //DISPLAY I2C ADDRESS

const uint GPIO_JOY_VRX = 27;       // GPIO Joystick for x-axis
const uint GPIO_JOY_VRY = 26;       //GPIO Joystick for y-axis
const uint GPIO_JOY_BUTTON = 22;    // GPIO Joyshtick pushbutton

const uint GPIO_LED_RED = 13;       // GPIO Red RGB led
const uint GPIO_LED_GREEN = 11;     // GPIO Green RGB led
const uint GPIO_LED_BLUE = 12;      // GPIO Blue RGB led

const uint GPIO_BUTTON_A = 5;       // GPIO Pushbutton A

const uint16_t PWM_WRAP = 4096;     // Max counter value for PWM

/* Function prototypes*/

uint pwm_setup(uint gpio);
void press_handler(uint gpio, uint32_t events);

/* Global variables*/

static volatile absolute_time_t joy_button_last_pressed_time, pushbutton_last_pressed_time;     // Stores the last time the button was pressed
static volatile bool led_green_state = false;       // Green led activation status
static volatile bool pwm_state = true;              // PWM activation status
static uint slice_red_led, slice_blue_led;          // PWM slices for blue and red leds            
static ssd1306_t display_ssd;                       // Display struct

int main(){
    uint16_t joy_vrx_adc, joy_vry_adc;      //ADC read values for x and y axis
    float duty_cycle_led_red, duty_cycle_led_blue;  //Duty cycle for x and y axis
    uint8_t cursor_position[] = {WIDTH/2 - 4, HEIGHT/2 - 4};    // Cursos position on the display
    int x_diff, y_diff;     //Difference between ADC values and center position

    /* ******************************************Peripheral configuration************************************************************* */
    stdio_init_all();

    /* PWM setup*/
    slice_red_led = pwm_setup(GPIO_LED_RED);
    slice_blue_led = pwm_setup(GPIO_LED_BLUE);

    /* ADC setup*/
    adc_init();
    adc_gpio_init(GPIO_JOY_VRX);
    adc_gpio_init(GPIO_JOY_VRY);

    /* Buttons setup*/

    //Joystick button
    gpio_init(GPIO_JOY_BUTTON);
    gpio_set_dir(GPIO_JOY_BUTTON, GPIO_IN);
    gpio_pull_up(GPIO_JOY_BUTTON);

    //Pushbutton A
    gpio_init(GPIO_BUTTON_A);
    gpio_set_dir(GPIO_BUTTON_A, GPIO_IN);
    gpio_pull_up(GPIO_BUTTON_A);

    //Pushbutton B (optional: for easy reboot)
    gpio_init(6);
    gpio_set_dir(6, GPIO_IN);
    gpio_pull_up(6);
    
    // Setup pushbuttons IRQ
    gpio_set_irq_enabled_with_callback(GPIO_JOY_BUTTON, GPIO_IRQ_EDGE_FALL, true, &press_handler);
    gpio_set_irq_enabled(GPIO_BUTTON_A, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(6, GPIO_IRQ_EDGE_FALL, true);

    /* Green led setup*/
    gpio_init(GPIO_LED_GREEN);
    gpio_set_dir(GPIO_LED_GREEN, GPIO_OUT);

    /* SSD1306 setup*/

    //Display initialization
    i2c_init(DISPLAY_I2C_PORT, DISPLAY_I2C_FREQ);

    gpio_set_function(DISPLAY_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(DISPLAY_I2C_SCL, GPIO_FUNC_I2C);

    gpio_pull_up(DISPLAY_I2C_SDA);
    gpio_pull_up(DISPLAY_I2C_SCL);

    ssd1306_init(&display_ssd, WIDTH, HEIGHT, false, DISPLAY_I2C_ADDRESS, DISPLAY_I2C_PORT);
    ssd1306_config(&display_ssd);
    ssd1306_send_data(&display_ssd);


    // Clear display and draw initial border and cursor

    ssd1306_fill(&display_ssd, false);    
    ssd1306_rect(&display_ssd, 3, 3, 122, 60, true, false);
    ssd1306_draw_string(&display_ssd, ".", cursor_position[0], cursor_position[1]);
    ssd1306_send_data(&display_ssd);
    
    
    /* Init debounce times*/
    joy_button_last_pressed_time = get_absolute_time();
    pushbutton_last_pressed_time = get_absolute_time();

    
    /* ************************************** Main loop ************************************************************* */

    while(true){
        //Read ADC value for x axis
        adc_select_input(1);
        joy_vrx_adc = adc_read();
        
        //Read ADC value for y axis
        adc_select_input(0);
        joy_vry_adc = adc_read();
        
        // Calculate difference between ADC values and center position
        x_diff = JOY_CENTER - joy_vrx_adc;
        y_diff = JOY_CENTER - joy_vry_adc;

        if(abs(x_diff) < CENTER_RADIUS){ //joystick is inside radius
            duty_cycle_led_red = 0;
        
        }else{  //joystick was moved
            duty_cycle_led_red = abs(x_diff);   //calculate the absolute difference (distance)
            // Calculate duty cycle based on the side that the joystick was moved (center position is not the middle)
            duty_cycle_led_red = (joy_vrx_adc < JOY_CENTER) ? duty_cycle_led_red/1873.0 : duty_cycle_led_red/1994.0; 
            
        }

        if(abs(y_diff) < CENTER_RADIUS){ //joystick is inside radius
            duty_cycle_led_blue = 0;
        }else{
            duty_cycle_led_blue = abs(y_diff);      //calculate the absolute difference (distance)
            // Calculate duty cycle based on the side that the joystick was moved (center position is not the middle)
            duty_cycle_led_blue = (joy_vry_adc <  JOY_CENTER) ? duty_cycle_led_blue/1864.0 : duty_cycle_led_blue/2003.0; 
        }
        
        /* Update cursor*/
        //Erase cursor
        ssd1306_draw_string(&display_ssd, " ", cursor_position[0], cursor_position[1]);

        // Calculate new position
        cursor_position[0] = WIDTH * (joy_vrx_adc/4095.0) - 4;
        cursor_position[1] = HEIGHT * (1 - (joy_vry_adc/4095.0)) - 4;

        // Borders delimitation
        cursor_position[0] = fmaxf(6, fminf(cursor_position[0], 116));
        cursor_position[1] = fmaxf(6, fminf(cursor_position[1], 52));
    

        //Display new cursor
        ssd1306_draw_string(&display_ssd, ".", cursor_position[0], cursor_position[1]);
        ssd1306_send_data(&display_ssd);

        /* Update pwm duty cycle*/
        pwm_set_gpio_level(GPIO_LED_RED, duty_cycle_led_red * PWM_WRAP);
        pwm_set_gpio_level(GPIO_LED_BLUE, duty_cycle_led_blue * PWM_WRAP);

        sleep_ms(100);


    }


}

/*
*   @brief Function used to initialize a gpio as pwm
*   @param gpio - pin to be initialized
*   @returns The slice num of the gpio
*/
uint pwm_setup(uint gpio){
    uint slice;
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    slice = pwm_gpio_to_slice_num(gpio);

    pwm_set_wrap(slice, PWM_WRAP);

    pwm_set_enabled(slice, true);

    return slice;

}

/*
*   @brief Callback for IRQ generatd by pushbuttons
*/

void press_handler(uint gpio, uint32_t events){
    absolute_time_t current_time = get_absolute_time(); //get current time for debounce


    /* Routine for joystick pushbutton*/
    if(gpio == GPIO_JOY_BUTTON && absolute_time_diff_us(joy_button_last_pressed_time, current_time) > DEBOUNCE_TIME){
        joy_button_last_pressed_time = current_time;

        //Toggle green led state
        led_green_state = !led_green_state;
        gpio_put(GPIO_LED_GREEN, led_green_state);

        // Update border according to green led state
        ssd1306_rect(&display_ssd, 6, 6, 117, 54, led_green_state, false);
        ssd1306_send_data(&display_ssd);

    }

    /* Routine for pushbutton A*/
    if(gpio == GPIO_BUTTON_A && absolute_time_diff_us(pushbutton_last_pressed_time, current_time) > DEBOUNCE_TIME){
        pushbutton_last_pressed_time = current_time;

        //Toggle pwm state
        pwm_state = !pwm_state;
        pwm_set_enabled(slice_blue_led, pwm_state);
        pwm_set_enabled(slice_red_led, pwm_state);
    }

    /* Routine for pushbutton B*/
    if(gpio == 6){
        reset_usb_boot(0,0); //bootsel
    }
}
