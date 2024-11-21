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
void init_i2c();
void enable_ports();
void i2c_start(uint32_t targadr, uint8_t size, uint8_t dir);
void i2c_stop();
void i2c_waitidle();
void i2c_clearnack();
int i2c_checknack();
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
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN;
    RCC->APB1ENR |= RCC_APB1ENR_I2C2EN;
    GPIOC->MODER &= ~0xffff;
    GPIOC->MODER |= 0x55 << (4*2);
    GPIOC->OTYPER &= ~0xff;
    GPIOC->OTYPER |= 0xf0;
    GPIOC->PUPDR &= ~0xff;
    GPIOC->PUPDR |= 0x55;

    GPIOA->MODER &= ~(GPIO_MODER_MODER11 | GPIO_MODER_MODER12);
    GPIOA->MODER |= (GPIO_MODER_MODER11_1 | GPIO_MODER_MODER12_1);

    GPIOA->AFR[1] &= ~0x000FF000;
    GPIOA->AFR[1] |= 0x00055000;
}



// I2C stuff:

void init_i2c(void) {
    RCC->APB1ENR |= RCC_APB1ENR_I2C2EN;

    I2C2->CR1 &= ~I2C_CR1_PE; // I2C disabled
    I2C2->CR1 |= I2C_CR1_ANFOFF;
    I2C2->CR1 |= I2C_CR1_ERRIE;
    // I2C2->CR1 |= I2C_CR1_ERRIE | I2C_CR1_NACKIE | I2C_CR1_STOPIE;
    I2C2->CR1 |= I2C_CR1_NOSTRETCH;

    // PRESC = 5 (divides 48 MHz by (PRESC + 1) to 8 MHz for I2CCLK)
    // SCLL = 0x09 (for the low period of SCL clock)
    // SCLH = 0x03 (for the high period of SCL clock)
    // SDADEL = 0x01 (data delay after SDA rising edge)
    // SCLDEL = 0x03 (delay between SDA and SCL change)
    I2C2->TIMINGR = (5 << 28) | (0x03 << 20) | (0x09 << 0) | (0x01 << 16) | (0x03 << 8);

    I2C2->CR2 &= ~I2C_CR2_ADD10;  // Set to 7-bit addressing mode
    I2C2->CR2 &= ~I2C_CR2_AUTOEND; // Automatically send STOP condition after the last byte

    I2C2->CR1 |= I2C_CR1_PE;
}

void i2c_start(uint32_t targadr, uint8_t size, uint8_t dir) {
    uint32_t tmpreg = I2C2->CR2;

    // Clear SADD, NBYTES, RD_WRN, START, STOP
    tmpreg &= ~(I2C_CR2_SADD | I2C_CR2_NBYTES | I2C_CR2_RD_WRN | I2C_CR2_START | I2C_CR2_STOP);

    // Set read/write direction in tmpreg.
    if (dir == 1) {
        tmpreg |= I2C_CR2_RD_WRN; 
    } else {
        tmpreg &= ~I2C_CR2_RD_WRN;
    }

    // 3. Set the target's address in SADD (shift targadr left by 1 bit) and the data size.
    tmpreg |= ((targadr<<1) & I2C_CR2_SADD) | ((size << 16) & I2C_CR2_NBYTES);

    // 4. Set the START bit.
    tmpreg |= I2C_CR2_START;

    // 5. Start the conversion by writing the modified value back to the CR2 register.
    I2C2->CR2 = tmpreg;
}

void i2c_stop(void) {
    // 0. If a STOP bit has already been sent, return from the function.
    // Check the I2C2 ISR register for the corresponding bit.
    if (I2C2->ISR & I2C_ISR_STOPF) {
        return;
    }

    // 1. Set the STOP bit in the CR2 register.
    I2C2->CR2 |= I2C_CR2_STOP;

    // 2. Wait until STOPF flag is (re???)set by checking the same flag in ISR.
    while (!(I2C2->ISR & I2C_ISR_STOPF));

    // 3. Clear the STOPF flag by writing 1 to the corresponding bit in the ICR.
    I2C2->ICR |= I2C_ICR_STOPCF;
}

void i2c_waitidle(void) {
    // nano_wait(1000000000000);
    while (I2C2->ISR & I2C_ISR_BUSY);
}

int8_t i2c_senddata(uint8_t targadr, uint8_t data[], uint8_t size) {
    
    // printf("in senddata\n");
    // 1. Wait until the I2C bus is idle
    i2c_waitidle();
    // printf("WRITE now idle\n");

    // printf("not idle\n");
    // 2. Send a START condition with the target address and write bit
    i2c_start(targadr, size, 0);  // 0 indicates a write operation

    // printf("WRITE start bit sent\n");

    for (uint8_t i = 0; i < size; i++) {

        int count = 0;
        while ((I2C2->ISR & I2C_ISR_TXIS) == 0) {
            count += 1;
            if (count > 1000000) {
                printf("SEND timeout\n");
                return -1;
            }
            if (i2c_checknack()) {
                printf("SEND first nack\n");
                i2c_clearnack();
                i2c_stop();
                return -1;
            }
        }
        // volatile char curr = data[i];

        printf("Writing (%d) %d (%c)\n", i, data[i] & I2C_TXDR_TXDATA, (char)(data[i] & I2C_TXDR_TXDATA));
        // printf("\n");
        I2C2->TXDR = data[i] & I2C_TXDR_TXDATA;
        // printf("writing %d (%c)\n", data[i], data[i]);
    }

    // printf("through loop\n");

    while (!(I2C2->ISR & I2C_ISR_TC) && !(I2C2->ISR & I2C_ISR_NACKF));

    // printf("TC and NAKKF flags not set\n");

    if (I2C2->ISR & I2C_ISR_NACKF) {
        printf("SEND last nack\n");
        // i2c_stop();
        // i2c_clearnack();
        return -1;  // No acknowledgment error
    }

    i2c_stop();

    return 0;
}

int i2c_recvdata(uint8_t targadr, uint8_t *data, uint8_t size) {

    // printf("here\n");
    // uint8_t *byte_data = (uint8_t*)data;  // Cast void* to uint8_t* for byte-wise access

    i2c_waitidle();
    // printf("READ now idle\n");

    // 2. Send a START condition with the target address and read bit (1)
    i2c_start(targadr, size, 1);  // 1 indicates a read operation
    // printf("READ start bit sent\n");

    // 3. Loop through each byte of data to receive
    for (uint8_t i = 0; i < size; i++) {
        // Wait until RXNE flag is set, or timeout if it takes too long
        int count = 0;
        while ((I2C2->ISR & I2C_ISR_RXNE) == 0) {
            count++;
            if (count > 1000000) {
                printf("RECV timeout\n");
                return -1;  // Timeout error
            }
            if (i2c_checknack()) {
                printf("RECV nack\n");
                i2c_clearnack();
                i2c_stop();
                return -1;  // NACK error
            }
        }

        // 4. Mask the received data in RXDR with I2C_RXDR_RXDATA to ensure it is 8 bits,
        //    then store it in the data buffer
        // byte_data[i] = I2C2->RXDR & I2C_RXDR_RXDATA;
        data[i] = I2C2->RXDR & I2C_RXDR_RXDATA;

        // printf("\n");
        printf("Reading (%d) %d (%c)\n", i, data[i], (char)(data[i]));
    }

    while(!(I2C2->ISR & I2C_ISR_TC) && !(I2C2->ISR & I2C_ISR_NACKF));

    if (!((I2C2->ISR & I2C_ISR_NACKF) == 0)) {
        return -1;
    }
    // // 5. Send a STOP condition after receiving all the data
    i2c_stop();

    return 0;  // Success
}

void i2c_clearnack(void) {
    I2C2->ICR |= I2C_ICR_NACKCF;
}

int i2c_checknack(void) {
    if (I2C2->ISR & I2C_ISR_NACKF) {
        return 1;  // NACK received
    }
    return 0;  // No NACK
}

#define EEPROM_ADDR 0x57

void eeprom_write(uint16_t loc, const char* data, uint8_t len) {
    printf("Starting Write\n");
    // printf("in eeprom write: loc: %d, data: %s, len: %d\n", loc, data, len);
    uint8_t bytes[34];
    bytes[0] = loc>>8;
    bytes[1] = loc&0xFF;
    for(int i = 0; i<len; i++){
        bytes[i+2] = data[i];
    }
    i2c_senddata(EEPROM_ADDR, bytes, len+2);
    printf("Done Writing\n");
}

void eeprom_read(uint16_t loc, char data[], uint8_t len) {
    // ... your code here
    // printf("in eeprom read: loc: %d, %s, len: %d\n", loc, data, len);
    printf("Starting Read\n");
    uint8_t bytes[2];
    bytes[0] = loc>>8;
    bytes[1] = loc&0xFF;
    i2c_senddata(EEPROM_ADDR, bytes, 2);
    i2c_recvdata(EEPROM_ADDR, data, len);
    printf("Done Reading\n");
}

// // end I2C stuff:




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
void write_matrix(int rgb)
{
    matrix[curY][curX] = rgb;
}


/*
function to flash a row of the matrix.
Selects the input row, and then buffers in the rgb
values stord in "matrix" at that row
*/
void display_row(int row)
{
    /*
    board displays two rows at a time, the selected
    row and the row below it. To maximize brightness,
    flash both
    */
    for (int j = -1; j < 1; j++)
    {
        //reset enable bit
        GPIOB->ODR |= 1 << 13;

        //reset data values
        GPIOB->ODR &= ~0b111111111111;

        //release latch
        GPIOB->ODR |= 1 << 12;

        //select current row, inverting y such that y++ is up, instead of down
        GPIOB->ODR |= (63 - row + j) << 6;

        //buffer in all zeros to clear the line
        for (int i = 0; i < 64; i++)
        {
            GPIOB->ODR |= 1 << 11;
            GPIOB->ODR &= ~(1 << 11);
        }
        //reset rgb[0:1] then set is to val stored in matrix at current location
        for (int i = 0; i < 64; i++)
        {
            GPIOB->ODR &= ~0b111111;
            GPIOB->ODR |= matrix[row][i] << (row >= 32 ? 0 : 3); //shift from r1g1b1 to r2g2b2 if y is below half way
            GPIOB->ODR |= 1 << 11;
            GPIOB->ODR &= ~(1 << 11);
        }
        //set latch
        GPIOB->ODR &= ~(1 << 12);

        //enable the row
        GPIOB->ODR &= ~(1 << 13);
    }
}

/* similar to display row:
selects the row of the cursor's Y position, and then buffers in
0's until the cursor's x position. Sets the cursor x position color
and then buffers in zeros until end of line
*/
void display_cursor()
{
    //disable row
    GPIOB->ODR |= 1 << 13;
    //board displays selected row and 1 below
    //loop over row above and current row so that
    //the current row is brighter
    for (int j = -1; j < 1; j++)
    {
        GPIOB->ODR &= ~0b111111111111; //reset data vals
        GPIOB->ODR |= 1 << 12; //reset latch

        /*
        R1 G1 B1 is color for top half, R2 G2 B2 is color for bottom
        set the color to be either 000111 = white top half, or 
        111000 = white bottom half depending on cursor Y value
        */
        int color = 0b111 << ((curY >= 32) ? 0 : 3);
        GPIOB->ODR |= (63 - curY + j) << 6; //invert matrix so y++ is up not down then set corresponding ODR vals

        //clear row
        for (int i = 0; i < 64; i++)
        {
            GPIOB->ODR |= 1 << 11;
            GPIOB->ODR &= ~(1 << 11);
        }
        //buffer zeros until cursor x position
        for (int i = 0; i < curX; i++)
        {
            GPIOB->ODR |= 1 << 11;
            GPIOB->ODR &= ~(1 << 11);
        }
        
        //set rgb[0:1] to color
        //then buffer in
        GPIOB->ODR |= color;
        GPIOB->ODR |= 1 << 11;
        GPIOB->ODR &= ~(1 << 11);

        //reset rgb[0:1] to all blank
        //and buffer into matrix until end of line
        GPIOB->ODR &= ~(0b111111);
        for (int i = curX + 1; i < 64; i++)
        {
            GPIOB->ODR |= 1 << 11;
            GPIOB->ODR &= ~(1 << 11);
        }

        //set latch
        GPIOB->ODR &= ~(1 << 12); 
    }

    //enable line
    GPIOB->ODR &= ~(1 << 13);
}

// whatever default vals we want displayed: could be all blank, or something else
void init_matrix()
{
    for (int i = 0; i < 64; i++)
    {
        for (int j = 0; j < 64; j++)
        {
            matrix[i][j] = 0;
        }
    }
}

void save_sketch() {
    uint16_t addr = 0; // Start of EEPROM memory
    for (int i = 0; i < 64; i++) {
        uint8_t row_data[64];
        for (int j = 0; j < 64; j++) {
            row_data[j] = matrix[i][j];
        }
        eeprom_write(addr, (char *)row_data, 64);
        addr += 64; // Move to the next row
    }
}

void reload_sketch() {
    uint16_t addr = 0; // Start of EEPROM memory
    for (int i = 0; i < 64; i++) {
        uint8_t row_data[64];
        eeprom_read(addr, (char *)row_data, 64);
        for (int j = 0; j < 64; j++) {
            matrix[i][j] = row_data[j];
        }
        addr += 64; // Move to the next row
    }
}

void delete_sketch() {
    // Clear the in-memory matrix
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 64; j++) {
            matrix[i][j] = 0;
        }
    }

    // Optionally clear the EEPROM memory
    uint16_t addr = 0; // Start of EEPROM memory
    uint8_t clear_row[64] = {0};
    for (int i = 0; i < 64; i++) {
        eeprom_write(addr, (char *)clear_row, 64);
        addr += 64; // Move to the next row
    }
}

void test_eeprom_functions() {
    // Test initial save
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 64; j++) {
            matrix[i][j] = (i + j) % 8; // Fill matrix with a test pattern
        }
    }
    save_sketch();
    spi1_display1("Test Save Done");

    // Clear the matrix and reload from EEPROM
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 64; j++) {
            matrix[i][j] = 0; // Clear matrix
        }
    }
    reload_sketch();
    spi1_display1("Test Reload Done");

    // Verify the reloaded data matches the saved pattern
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 64; j++) {
            if (matrix[i][j] != (i + j) % 8) {
                spi1_display1("Test Failed");
                return;
            }
        }
    }

    // Delete the sketch
    delete_sketch();
    spi1_display1("Test Delete Done");

    // Verify the matrix is cleared
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 64; j++) {
            if (matrix[i][j] != 0) {
                spi1_display1("Test Failed");
                return;
            }
        }
    }

    spi1_display1("All Tests Passed");
}


//===========================================================================
// Main function
//===========================================================================

int main(void) {
    internal_clock();
    enable_ports();
    init_i2c();
    init_tim7();
    init_spi1();
    spi1_init_oled();

    int selected_r = 0, selected_g = 0, selected_b = 0;
    char event;
    char myString[] = {'x', '=', '0', '0', ' ', 'y', '=', '0', '0', '\0'};

    for (;;) {
        event = get_keypress();

        if (event == 'A') {
            selected_r = !selected_r;
            spi1_display1(selected_r ? "Red ON" : "Red OFF");
        } else if (event == 'B') {
            selected_g = !selected_g;
            spi1_display1(selected_g ? "Green ON" : "Green OFF");
        } else if (event == 'C') {
            selected_b = !selected_b;
            spi1_display1(selected_b ? "Blue ON" : "Blue OFF");
        }

        if (event == '6' && curX < 63)
            curX++;
        else if (event == '4' && curX > 0)
            curX--;
        else if (event == '5' && curY > 0)
            curY--;
        else if (event == '2' && curY < 63)
            curY++;

        if (event == 'D') {
            save_sketch();
            spi1_display1("Saved!");
        } else if (event == '#') {
            reload_sketch();
            spi1_display1("Reloaded!");
        } else if (event == '*') {
            delete_sketch();
            spi1_display1("Deleted!");
        } else if (event == '1') {  // Trigger the EEPROM test function
            test_eeprom_functions();
        }

        write_matrix(selected_r | (selected_g << 1) | (selected_b << 2));
        display_cursor();

        myString[3] = (char)(curX % 10 + 48);
        myString[2] = (char)(curX / 10 + 48);
        myString[8] = (char)(curY % 10 + 48);
        myString[7] = (char)(curY / 10 + 48);
        spi1_display2(myString);
    }
}

