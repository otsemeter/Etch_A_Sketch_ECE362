/**
  ******************************************************************************
  * @file    main.c
  * @author  Weili An, Niraj Menon
  * @date    Feb 3, 2024
  * @brief   ECE 362 Lab 6 Student template
  ******************************************************************************
*/

/*******************************************************************************/

// Fill out your username, otherwise your completion code will have the 
// wrong username!
char* username = "osemeter";

/*******************************************************************************/ 

#include "stm32f0xx.h"

char matrix [64][25];
int curX = 0;
int curY = 0;
void set_char_msg(int, char);
void nano_wait(unsigned int);
void game(void);
void internal_clock();
void check_wiring();
void autotest();

//===========================================================================
// Configure GPIOC
//===========================================================================
void enable_ports(void) {
    // Only enable port C for the keypad
    RCC->AHBENR |= RCC_AHBENR_GPIOCEN;
    GPIOC->MODER &= ~0xffff;
    GPIOC->MODER |= 0x55 << (4*2);
    GPIOC->OTYPER &= ~0xff;
    GPIOC->OTYPER |= 0xf0;
    GPIOC->PUPDR &= ~0xff;
    GPIOC->PUPDR |= 0x55;
}


uint8_t col; // the column being scanned

void drive_column(int);   // energize one of the column outputs
int  read_rows();         // read the four row inputs
void update_history(int col, int rows); // record the buttons of the driven column
char get_key_event(void); // wait for a button event (press or release)
char get_keypress(void);  // wait for only a button press event.
float getfloat(void);     // read a floating-point number from keypad
void show_keys(void);     // demonstrate get_key_event()

//===========================================================================
// Bit Bang SPI LED Array
//===========================================================================
int msg_index = 0;
uint16_t msg[8] = { 0x0000,0x0100,0x0200,0x0300,0x0400,0x0500,0x0600,0x0700 };
extern const char font[];

//===========================================================================
// Configure PB12 (CS), PB13 (SCK), and PB15 (SDI) for outputs
//===========================================================================
void setup_bb(void) {
    RCC->AHBENR |= RCC_AHBENR_GPIOBEN;
    GPIOB->MODER &= ~0xcf000000;
    GPIOB->MODER |= 0x45000000;
    GPIOB->BSRR = 0x20001000;
}

void small_delay(void) {
    nano_wait(50000);
}

//===========================================================================
// Set the MOSI bit, then set the clock high and low.
// Pause between doing these steps with small_delay().
//===========================================================================
void bb_write_bit(int val) {
    // CS (PB12)
    // SCK (PB13)
    // SDI (PB15)
    GPIOB->BSRR = 0x1 << (val == 0?31:15);
    small_delay();
    GPIOB->BSRR = 0x1 << 13;
    small_delay();
    GPIOB->BRR = 0x1 << 13;
}

//===========================================================================
// Set CS (PB12) low,
// write 16 bits using bb_write_bit,
// then set CS high.
//===========================================================================
void bb_write_halfword(int halfword) {
    GPIOB->BRR = 0x1 << 12;
    for(int i = 15; i >= 0; i-- )
        bb_write_bit((halfword & (1 << i)) >> i);
    GPIOB->BSRR = 0x1 << 12;
}

//===========================================================================
// Continually bitbang the msg[] array.
//===========================================================================
void drive_bb(void) {
    for(;;)
        for(int d=0; d<8; d++) {
            bb_write_halfword(msg[d]);
            nano_wait(1000000); // wait 1 ms between digits
        }
}

//============================================================================
// Configure Timer 15 for an update rate of 1 kHz.
// Trigger the DMA channel on each update.
// Copy this from lab 4 or lab 5.
//============================================================================
void init_tim15(void) {
    RCC->APB2ENR |= RCC_APB2ENR_TIM15EN;
    TIM15->PSC = 4800 - 1;
    TIM15->ARR = 10 - 1;
    TIM15->DIER |= TIM_DIER_UDE;
    TIM15->CR1 |= TIM_CR1_CEN;
}


//===========================================================================
// Configure timer 7 to invoke the update interrupt at 1kHz
// Copy from lab 4 or 5.
//===========================================================================
void init_tim7(void) {
    RCC->APB1ENR |= RCC_APB1ENR_TIM7EN;
    TIM7->PSC = 4800 - 1;
    TIM7->ARR = 10 - 1;
    TIM7->DIER |= TIM_DIER_UIE;
    NVIC->ISER[0] = 1 << TIM7_IRQn; 
    TIM7->CR1 |= TIM_CR1_CEN;
}


//===========================================================================
// Copy the Timer 7 ISR from lab 5
//===========================================================================
void TIM7_IRQHandler()
{
    TIM7->SR &= ~TIM_SR_UIF;
    int rows = read_rows();
    update_history(col, rows);
    col = (col + 1) & 3;
    drive_column(col);
}


//===========================================================================
// Initialize the SPI2 peripheral.
//===========================================================================
void init_spi2(void) {

    RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;
    RCC->AHBENR |= RCC_AHBENR_GPIOBEN;
    GPIOB->MODER &= ~0xcf000000; 
    GPIOB->MODER |= 0x8a000000;
    GPIOB->AFR[1] &= ~0xf0ff0000;
    SPI2->CR1 &= ~SPI_CR1_SPE;
    SPI2->CR1 |= SPI_CR1_BR | SPI_CR1_MSTR;
    SPI2->CR2 |= SPI_CR2_NSSP | SPI_CR2_SSOE | SPI_CR2_TXDMAEN | SPI_CR2_DS;
    SPI2->CR1 |= SPI_CR1_SPE;
}

//===========================================================================
// Configure the SPI2 peripheral to trigger the DMA channel when the
// transmitter is empty.  Use the code from setup_dma from lab 5.
//===========================================================================
void spi2_setup_dma(void) {
    DMA1_Channel5->CCR &= ~DMA_CCR_EN;
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;
    DMA1_Channel5->CMAR = (uint32_t)(msg);
    DMA1_Channel5->CPAR = (uint32_t)(&(SPI2->DR));
    DMA1_Channel5->CNDTR = 8;
    DMA1_Channel5->CCR |= DMA_CCR_DIR;
    DMA1_Channel5->CCR |= DMA_CCR_MINC;
    DMA1_Channel5->CCR |= DMA_CCR_MSIZE_0;
    DMA1_Channel5->CCR |= DMA_CCR_PSIZE_0;
    DMA1_Channel5->CCR |= DMA_CCR_CIRC;
    SPI2->CR2 |= SPI_CR2_TXDMAEN;
}

//===========================================================================
// Enable the DMA channel.
//===========================================================================
void spi2_enable_dma(void) {
    DMA1_Channel5->CCR |= DMA_CCR_EN;
}

//===========================================================================
// 4.4 SPI OLED Display
//===========================================================================
void init_spi1() {
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN;
    GPIOA->MODER &= ~0xc000cc00;
    GPIOA->MODER |= 0x80008800;
    GPIOA->AFR[1] &= ~0xf0000000;
    GPIOA->AFR[0] &= ~0xf0f00000;
    SPI1->CR1 &= ~SPI_CR1_SPE;
    SPI1->CR1 |= SPI_CR1_BR | SPI_CR1_MSTR;
    SPI1->CR2 |= SPI_CR2_NSSP | SPI_CR2_SSOE | SPI_CR2_TXDMAEN | SPI_CR2_DS_3 | SPI_CR2_DS_0;
    SPI1->CR2 &= ~SPI_CR2_DS_2;
    SPI1->CR2 &= ~SPI_CR2_DS_1;
    SPI1->CR1 |= SPI_CR1_SPE;
}
void spi_cmd(unsigned int data) {
    while(!(SPI1->SR & SPI_SR_TXE));
    SPI1->DR = data;
}
void spi_data(unsigned int data) {
    spi_cmd(data | 0x200);
}
void spi1_init_oled() {
    nano_wait(1000000);//wait 1 ms
    spi_cmd(0x38);//fuction set (idk what that means)
    small_delay();
    spi_cmd(0x08);//turn display off
    small_delay();
    spi_cmd(0x01);//clear display
    nano_wait(2000000);//wait 2 ms
    spi_cmd(0x06);//set entry mode
    small_delay();
    spi_cmd(0x02);//move cursor to home position
    small_delay();
    spi_cmd(0x0c);//turn on display
}
void spi1_display1(const char *string) {
    spi_cmd(0x02);//move cursor to home position
    int idx = 0;
    while(string[idx] != '\0')
    {
        spi_data(string[idx++]);
    }
}
void spi1_display2(const char *string) {
    spi_cmd(0xc0);
    int idx = 0;
    while(string[idx] != '\0')
    {
        spi_data(string[idx++]);
    }
}

//===========================================================================
// This is the 34-entry buffer to be copied into SPI1.
// Each element is a 16-bit value that is either character data or a command.
// Element 0 is the command to set the cursor to the first position of line 1.
// The next 16 elements are 16 characters.
// Element 17 is the command to set the cursor to the first position of line 2.
//===========================================================================
uint16_t display[34] = {
        0x002, // Command to set the cursor at the first position line 1
        0x200+'E', 0x200+'C', 0x200+'E', 0x200+'3', 0x200+'6', + 0x200+'2', 0x200+' ', 0x200+'i',
        0x200+'s', 0x200+' ', 0x200+'t', 0x200+'h', + 0x200+'e', 0x200+' ', 0x200+' ', 0x200+' ',
        0x0c0, // Command to set the cursor at the first position line 2
        0x200+'c', 0x200+'l', 0x200+'a', 0x200+'s', 0x200+'s', + 0x200+' ', 0x200+'f', 0x200+'o',
        0x200+'r', 0x200+' ', 0x200+'y', 0x200+'o', + 0x200+'u', 0x200+'!', 0x200+' ', 0x200+' ',
};

//===========================================================================
// Configure the proper DMA channel to be triggered by SPI1_TX.
// Set the SPI1 peripheral to trigger a DMA when the transmitter is empty.
//===========================================================================
void spi1_setup_dma(void) {
    DMA1_Channel3->CCR &= ~DMA_CCR_EN;
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;
    DMA1_Channel3->CMAR = (uint32_t)(display);
    DMA1_Channel3->CPAR = (uint32_t)(&(SPI1->DR));
    DMA1_Channel3->CNDTR = 34;
    DMA1_Channel3->CCR |= DMA_CCR_DIR;
    DMA1_Channel3->CCR |= DMA_CCR_MINC;
    DMA1_Channel3->CCR |= DMA_CCR_MSIZE_0;
    DMA1_Channel3->CCR |= DMA_CCR_PSIZE_0;
    DMA1_Channel3->CCR |= DMA_CCR_CIRC;
    SPI1->CR2 |= SPI_CR2_TXDMAEN;
}

//===========================================================================
// Enable the DMA channel triggered by SPI1_TX.
//===========================================================================
void spi1_enable_dma(void) {
    
    DMA1_Channel3->CCR |= DMA_CCR_EN;
}


//set RGB value at cursor location in matrix
void write_matrix(int r, int g, int b)
{
    if(r)
        matrix[curY][curX / 8] |= 0x1 << (7 - curX % 8);
    else
        matrix[curY][curX / 8] &= ~(0x1 << (7 - curX % 8));

     if(g)
        matrix[curY][curX / 8 + 8] |= 0x1 << (7 - curX % 8);
    else
        matrix[curY][curX / 8 + 8] &= ~(0x1 << (7 - curX % 8));
    
     if(b)
        matrix[curY][curX / 8 + 16] |= 0x1 << (7 - curX % 8);
    else
        matrix[curY][curX / 8 + 16] &= ~(0x1 << (7 - curX % 8));
}


//get either R G or B value at cursor location
int read_matrix(int rgb)
{
    int n;
    if(rgb == 0)
        n = 0;
    else if(rgb == 1)
        n = 8;
    else
        n = 16;

    return (matrix[curY][curX / 8 + n] & 1 << (7 - curX % 8)) >> (7 - curX % 8);
}


//===========================================================================
// an update the led matrix function to be called everytime the cursor moves (i think?)
//===========================================================================
/* the logic here could be a little wrong based on my understanding of bb_write_bit and whether 
or not that's what's interacting with the led matrix? but generally this should be the right idea for 
iterating through the led matrix to update each row*/
//===========================================================================
// ADDITIONALLY this needs to be implemented in main in some way, similar to an interrupt? i'm not sure yet

// specifically works by going through the matrix and sending each bit to the led matrix through bb_write_bit
// if the matrix was changed by the cursor moving, it should update the led matrix 
void update_led_matrix(void) {
    int row_num, col_num, bit_num;
    uint8_t byte_num;

    // go through each row at a time
    for(row_num = 0; row_num < 64; row_num++) {

        // each byte/column in the current row
        for(col_num = 0; col_num < 25; col++) {

            byte_num = matrix[row_num][col_num];    // gets the byte from each column

            // iterate through each bit of the current byte (most sig to least sig bit)
            for(bit_num = 7; bit_num >= 0; bit_num--){

                // IM NOT SURE IF THIS IS CORRECTLY TAKING INTO ACCOUNT THE RGB VALUE
                bb_write_bit((byte_num >> bit_num) & 1);    // isolated bit it sent to bb_write_bit which sends it to the LED matrix
            }
        }

        small_delay();  // short delay between processing rows to ensure it works well
    }
    return;
}

// add a the shake/clear function
void shake(void) {

}

// add save function 
void save_sketch(void){

}

// add reload function
void reload_sketch(void){

}

// add delete function
void delete_sketch(void){
    
}

//===========================================================================
// Main function
//===========================================================================

int main(void) {
    internal_clock();
    enable_ports();
    init_tim7();
    char event;
    init_spi1();
    spi1_init_oled();
    char myString [] = {'x', '=', '0', '0', ' ', 'y', '=', '0', '0', '\0'};
    
    for(;;)
    {
        event = get_keypress();
        if(event == 'A' && curX <64)
            curX++;
        else if(event == 'B' && curX > 0)
            curX--;
        else if(event == '*' && curY > 0)
            curY--;
        else if(event == '0' && curY < 64)
            curY++;
        
        write_matrix(1, 0, 1);
        char rgb[] = {'r', '=', (char)(read_matrix(0) + 48), ' ', 'g', '=', (char)(read_matrix(1) + 48), 'b', '=', (char)(read_matrix(2) + 48)};
        spi1_display1(rgb);
        myString[3] = (char)(curX % 10 + 48);
        myString[2] = (char)(curX / 10 + 48);
        myString[8] = (char)(curY % 10 + 48);
        myString[7] = (char)(curY / 10 + 48);
        spi1_display2(myString);
    }
}
