#include "stm32f0xx.h"

// Global Variables
int matrix[64][64]; // Global matrix holding current board configuration
int curX = 0;       // Cursor x position
int curY = 0;       // Cursor y position
int curColor = 1;   // Current color defaulted to red
int dispRow = 0;    // Current row being displayed, defaulted to row 0
uint8_t col;        // The column being scanned
int msg_index = 0;
uint16_t msg[8] = {0x0000, 0x0100, 0x0200, 0x0300, 0x0400, 0x0500, 0x0600, 0x0700};
extern const char font[];

// Function Declarations
void set_char_msg(int, char);
void nano_wait(unsigned int);
void internal_clock();
void check_wiring();
void display_row(int row); // Display selected row on matrix
void display_cursor();     // Display cursor on matrix

void enable_ports(void);
void enable_b(void);
void drive_column(int);   // Energize one of the column outputs
int read_rows(void);      // Read the four row inputs
void update_history(int col, int rows); // Record the buttons of the driven column
char get_key_event(void); // Wait for a button event (press or release)
char get_keypress(void);  // Wait for only a button press event.
float getfloat(void);     // Read a floating-point number from keypad
void show_keys(void);     // Demonstrate get_key_event()

void init_tim15(void);
void init_tim7(void);
void init_tim6(void);
void TIM7_IRQHandler(void);
void TIM6_DAC_IRQHandler(void);
void init_spi2(void);
void spi2_setup_dma(void);
void spi2_enable_dma(void);
void init_spi1(void);
void spi_cmd(unsigned int data);
void spi_data(unsigned int data);
void spi1_init_oled(void);
void spi1_display1(const char *string);
void spi1_display2(const char *string);
void spi1_setup_dma(void);
void spi1_enable_dma(void);
void write_matrix(int rgb);
void init_matrix(void);

void small_delay(void);

// I2C Functions
void init_i2c(void);
void i2c_start(uint32_t targadr, uint8_t size, uint8_t dir);
void i2c_stop(void);
void i2c_waitidle(void);
void i2c_clearnack(void);
int i2c_checknack(void);
int8_t i2c_senddata(uint8_t targadr, uint8_t data[], uint8_t size);
int i2c_recvdata(uint8_t targadr, uint8_t *data, uint8_t size);
void eeprom_write(uint16_t loc, const char* data, uint8_t len);
void eeprom_read(uint16_t loc, char data[], uint8_t len);
void save_drawing(void);
void load_drawing(void);

#define EEPROM_ADDR 0x57

//========================================================================
// Configure GPIOC and GPIOA
//========================================================================
void enable_ports(void)
{
    // Enable port C for the keypad
    RCC->AHBENR |= RCC_AHBENR_GPIOCEN;
    // Enable port A for SPI1 and I2C
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN;

    // Configure GPIOC for keypad
    GPIOC->MODER &= ~0xffff;
    GPIOC->MODER |= 0x55 << (4 * 2);
    GPIOC->OTYPER &= ~0xff;
    GPIOC->OTYPER |= 0xf0;
    GPIOC->PUPDR &= ~0xff;
    GPIOC->PUPDR |= 0x55;

    // Configure GPIOA for I2C (Pins PA11 and PA12)
    GPIOA->MODER &= ~(GPIO_MODER_MODER11 | GPIO_MODER_MODER12);
    GPIOA->MODER |= (GPIO_MODER_MODER11_1 | GPIO_MODER_MODER12_1);
    GPIOA->AFR[1] &= ~0x000FF000;
    GPIOA->AFR[1] |= 0x00055000;
}

// Configure GPIOB
void enable_b(void)
{
    RCC->AHBENR |= RCC_AHBENR_GPIOBEN;
    GPIOB->MODER &= ~0xFFFFFFF;
    GPIOB->MODER |= 0x5555555;
}

//========================================================================
// Initialize I2C
//========================================================================
void init_i2c(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_I2C2EN;

    I2C2->CR1 &= ~I2C_CR1_PE; // Disable I2C peripheral
    I2C2->CR1 |= I2C_CR1_ANFOFF; // Disable analog noise filter
    I2C2->CR1 |= I2C_CR1_ERRIE; // Enable error interrupts
    I2C2->CR1 |= I2C_CR1_NOSTRETCH; // Disable clock stretching

    // Configure timing for 100kHz I2C clock
    I2C2->TIMINGR = (5 << 28) | (0x03 << 20) | (0x09 << 0) | (0x01 << 16) | (0x03 << 8);

    I2C2->CR2 &= ~I2C_CR2_ADD10;   // 7-bit addressing mode
    I2C2->CR2 &= ~I2C_CR2_AUTOEND; // Manual STOP condition

    I2C2->CR1 |= I2C_CR1_PE; // Enable I2C peripheral
}

//========================================================================
// I2C Helper Functions
//========================================================================
void i2c_waitidle(void)
{
    while (I2C2->ISR & I2C_ISR_BUSY)
        ;
}

void i2c_start(uint32_t devaddr, uint8_t size, uint8_t dir)
{
    uint32_t tmpreg = I2C2->CR2;

    // Clear SADD, NBYTES, RD_WRN, START, STOP bits
    tmpreg &= ~(I2C_CR2_SADD | I2C_CR2_NBYTES |
                I2C_CR2_RD_WRN | I2C_CR2_START | I2C_CR2_STOP);

    // Set read/write direction
    if (dir == 1)
    {
        tmpreg |= I2C_CR2_RD_WRN;
    }
    else
    {
        tmpreg &= ~I2C_CR2_RD_WRN;
    }

    // Set device address, number of bytes, and generate START condition
    tmpreg |= ((devaddr << 1) & I2C_CR2_SADD) |
              ((size << 16) & I2C_CR2_NBYTES) |
              I2C_CR2_START;

    I2C2->CR2 = tmpreg;
}

void i2c_stop(void)
{
    // Check if STOP bit is already set
    if (I2C2->ISR & I2C_ISR_STOPF)
        return;

    // Generate STOP condition
    I2C2->CR2 |= I2C_CR2_STOP;

    // Wait until STOP condition is generated
    while (!(I2C2->ISR & I2C_ISR_STOPF))
        ;

    // Clear STOP flag
    I2C2->ICR |= I2C_ICR_STOPCF;
}

int i2c_checknack(void)
{
    if (I2C2->ISR & I2C_ISR_NACKF)
        return 1; // NACK received
    return 0;
}

void i2c_clearnack(void)
{
    I2C2->ICR |= I2C_ICR_NACKCF;
}

int8_t i2c_senddata(uint8_t devaddr, uint8_t data[], uint8_t size)
{
    i2c_waitidle();
    i2c_start(devaddr, size, 0);

    for (uint8_t i = 0; i < size; i++)
    {
        int count = 0;
        while ((I2C2->ISR & I2C_ISR_TXIS) == 0)
        {
            count += 1;
            if (count > 1000000)
            {
                return -1;
            }
            if (i2c_checknack())
            {
                i2c_clearnack();
                i2c_stop();
                return -1;
            }
        }
        I2C2->TXDR = data[i] & I2C_TXDR_TXDATA;
    }

    while (!(I2C2->ISR & I2C_ISR_TC) && !(I2C2->ISR & I2C_ISR_NACKF))
        ;

    if (I2C2->ISR & I2C_ISR_NACKF)
    {
        return -1; // No acknowledgment error
    }

    i2c_stop();

    return 0;
}

int i2c_recvdata(uint8_t devaddr, uint8_t *data, uint8_t size)
{
    i2c_waitidle();
    i2c_start(devaddr, size, 1);

    for (uint8_t i = 0; i < size; i++)
    {
        int count = 0;
        while ((I2C2->ISR & I2C_ISR_RXNE) == 0)
        {
            count++;
            if (count > 1000000)
            {
                return -1; // Timeout error
            }
            if (i2c_checknack())
            {
                i2c_clearnack();
                i2c_stop();
                return -1; // NACK error
            }
        }
        data[i] = I2C2->RXDR & I2C_RXDR_RXDATA;
    }

    while (!(I2C2->ISR & I2C_ISR_TC) && !(I2C2->ISR & I2C_ISR_NACKF))
        ;

    if (I2C2->ISR & I2C_ISR_NACKF)
    {
        return -1;
    }
    i2c_stop();

    return 0;
}

void eeprom_write(uint16_t loc, const char *data, uint8_t len)
{
    uint8_t bytes[34];
    bytes[0] = loc >> 8;
    bytes[1] = loc & 0xFF;
    for (int i = 0; i < len; i++)
    {
        bytes[i + 2] = data[i];
    }
    i2c_senddata(EEPROM_ADDR, bytes, len + 2);
}

void eeprom_read(uint16_t loc, char data[], uint8_t len)
{
    uint8_t bytes[2];
    bytes[0] = loc >> 8;
    bytes[1] = loc & 0xFF;
    i2c_senddata(EEPROM_ADDR, bytes, 2);
    i2c_recvdata(EEPROM_ADDR, (uint8_t *)data, len);
}

//========================================================================
// Save and Load Drawing Functions
//========================================================================
void save_drawing(void)
{
    uint8_t data[4096];
    for (int y = 0; y < 64; y++)
    {
        for (int x = 0; x < 64; x++)
        {
            data[y * 64 + x] = (uint8_t)matrix[y][x];
        }
    }
    uint16_t addr = 0;
    for (int i = 0; i < 4096; i += 32)
    {
        eeprom_write(addr + i, (const char *)&data[i], 32);
        nano_wait(5000000); // Wait for EEPROM write cycle
    }
}

void load_drawing(void)
{
    uint8_t data[4096];
    uint16_t addr = 0;
    for (int i = 0; i < 4096; i += 32)
    {
        eeprom_read(addr + i, (char *)&data[i], 32);
        nano_wait(5000000); // Wait for EEPROM read cycle
    }
    for (int y = 0; y < 64; y++)
    {
        for (int x = 0; x < 64; x++)
        {
            matrix[y][x] = data[y * 64 + x];
        }
    }
}

//========================================================================
// Timer and Interrupt Initialization
//========================================================================
void init_tim15(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_TIM15EN;
    TIM15->PSC = 4800 - 1;
    TIM15->ARR = 10 - 1;
    TIM15->DIER |= TIM_DIER_UDE;
    TIM15->CR1 |= TIM_CR1_CEN;
}

void init_tim7(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM7EN;
    TIM7->PSC = 4800 - 1;
    TIM7->ARR = 10 - 1;
    TIM7->DIER |= TIM_DIER_UIE;
    NVIC->ISER[0] = 1 << TIM7_IRQn;
    TIM7->CR1 |= TIM_CR1_CEN;
}

void init_tim6(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM6EN;
    TIM6->PSC = 480 - 1;
    TIM6->ARR = 18 - 1;
    TIM6->DIER |= TIM_DIER_UIE;
    NVIC->ISER[0] = 1 << TIM6_DAC_IRQn;
    TIM6->CR1 |= TIM_CR1_CEN;
}

//========================================================================
// Timer Interrupt Service Routines
//========================================================================
void TIM7_IRQHandler(void)
{
    TIM7->SR &= ~TIM_SR_UIF;
    int rows = read_rows();
    update_history(col, rows);
    col = (col + 1) & 3;
    drive_column(col);
    display_cursor();
}

void TIM6_DAC_IRQHandler(void)
{
    TIM6->SR &= ~TIM_SR_UIF;

    display_row(dispRow++);
    if (dispRow >= 64)
        dispRow = 0;
}

//========================================================================
// SPI Initialization
//========================================================================
void init_spi2(void)
{
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

void spi2_setup_dma(void)
{
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

void spi2_enable_dma(void)
{
    DMA1_Channel5->CCR |= DMA_CCR_EN;
}

//========================================================================
// SPI1 OLED Display Functions
//========================================================================
void init_spi1(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN;
    GPIOA->MODER &= ~0xc000cc00;
    GPIOA->MODER |= 0x80008800;
    GPIOA->AFR[1] &= ~0xf0000000;
    GPIOA->AFR[0] &= ~0xf0f00000;
    SPI1->CR1 &= ~SPI_CR1_SPE;
    SPI1->CR1 |= SPI_CR1_BR | SPI_CR1_MSTR;
    SPI1->CR2 |= SPI_CR2_NSSP | SPI_CR2_SSOE | SPI_CR2_TXDMAEN |
                 SPI_CR2_DS_3 | SPI_CR2_DS_0;
    SPI1->CR2 &= ~SPI_CR2_DS_2;
    SPI1->CR2 &= ~SPI_CR2_DS_1;
    SPI1->CR1 |= SPI_CR1_SPE;
}

void spi_cmd(unsigned int data)
{
    while (!(SPI1->SR & SPI_SR_TXE))
        ;
    SPI1->DR = data;
}

void spi_data(unsigned int data)
{
    spi_cmd(data | 0x200);
}

void small_delay(void)
{
    nano_wait(50000); // 50 us delay
}

void spi1_init_oled(void)
{
    nano_wait(1000000); // Wait 1 ms
    spi_cmd(0x38);      // Function set
    small_delay();
    spi_cmd(0x08); // Display off
    small_delay();
    spi_cmd(0x01);      // Clear display
    nano_wait(2000000); // Wait 2 ms
    spi_cmd(0x06);      // Entry mode set
    small_delay();
    spi_cmd(0x02); // Return home
    small_delay();
    spi_cmd(0x0c); // Display on
}

void spi1_display1(const char *string)
{
    spi_cmd(0x02); // Move cursor to home position
    int idx = 0;
    while (string[idx] != '\0')
    {
        spi_data(string[idx++]);
    }
}

void spi1_display2(const char *string)
{
    spi_cmd(0xc0); // Move cursor to second line
    int idx = 0;
    while (string[idx] != '\0')
    {
        spi_data(string[idx++]);
    }
}

//========================================================================
// Define the display array used in spi1_dma_display1 and spi1_dma_display2
//========================================================================
uint16_t display[34] = {
    0x002, // Command to set the cursor at the first position line 1
    0x200 + 'E',
    0x200 + 'C',
    0x200 + 'E',
    0x200 + '3',
    0x200 + '6',
    0x200 + '2',
    0x200 + ' ',
    0x200 + 'i',
    0x200 + 's',
    0x200 + ' ',
    0x200 + 't',
    0x200 + 'h',
    0x200 + 'e',
    0x200 + ' ',
    0x200 + ' ',
    0x200 + ' ',
    0x0c0, // Command to set the cursor at the first position line 2
    0x200 + 'c',
    0x200 + 'l',
    0x200 + 'a',
    0x200 + 's',
    0x200 + 's',
    0x200 + ' ',
    0x200 + 'f',
    0x200 + 'o',
    0x200 + 'r',
    0x200 + ' ',
    0x200 + 'y',
    0x200 + 'o',
    0x200 + 'u',
    0x200 + '!',
    0x200 + ' ',
    0x200 + ' ',
};

//========================================================================
// SPI1 DMA Functions (Using the display array)
//========================================================================
void spi1_setup_dma(void)
{
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

void spi1_enable_dma(void)
{
    DMA1_Channel3->CCR |= DMA_CCR_EN;
}

//========================================================================
// Matrix Functions
//========================================================================
void write_matrix(int rgb)
{
    matrix[curY][curX] = rgb;
}

void init_matrix(void)
{
    for (int i = 0; i < 64; i++)
    {
        for (int j = 0; j < 64; j++)
        {
            matrix[i][j] = 0;
        }
    }
}

//========================================================================
// Definition of display_row function
//========================================================================
void display_row(int row)
{
    for (int j = -1; j < 1; j++)
    {
        // Reset enable bit
        GPIOB->ODR |= 1 << 13;

        // Reset data values
        GPIOB->ODR &= ~0b111111111111;

        // Release latch
        GPIOB->ODR |= 1 << 12;

        // Select current row, inverting y such that y++ is up, instead of down
        GPIOB->ODR |= (63 - row + j) << 6;

        // Buffer in all zeros to clear the line
        for (int i = 0; i < 64; i++)
        {
            GPIOB->ODR |= 1 << 11;
            GPIOB->ODR &= ~(1 << 11);
        }
        // Reset rgb[0:1] then set it to val stored in matrix at current location
        for (int i = 0; i < 64; i++)
        {
            GPIOB->ODR &= ~0b111111;
            GPIOB->ODR |= matrix[row][i] << (row >= 32 ? 0 : 3); // Shift from r1g1b1 to r2g2b2 if y is below half way
            GPIOB->ODR |= 1 << 11;
            GPIOB->ODR &= ~(1 << 11);
        }
        // Set latch
        GPIOB->ODR &= ~(1 << 12);

        // Enable the row
        GPIOB->ODR &= ~(1 << 13);
    }
}

//========================================================================
// Definition of display_cursor function
//========================================================================
void display_cursor()
{
    // Disable row
    GPIOB->ODR |= 1 << 13;
    // Board displays selected row and 1 below
    // Loop over row above and current row so that
    // the current row is brighter
    for (int j = -1; j < 1; j++)
    {
        GPIOB->ODR &= ~0b111111111111; // Reset data vals
        GPIOB->ODR |= 1 << 12;         // Reset latch

        int color = 0b111 << ((curY >= 32) ? 0 : 3);
        GPIOB->ODR |= (63 - curY + j) << 6; // Invert matrix so y++ is up not down then set corresponding ODR vals

        // Clear row
        for (int i = 0; i < 64; i++)
        {
            GPIOB->ODR |= 1 << 11;
            GPIOB->ODR &= ~(1 << 11);
        }
        // Buffer zeros until cursor x position
        for (int i = 0; i < curX; i++)
        {
            GPIOB->ODR |= 1 << 11;
            GPIOB->ODR &= ~(1 << 11);
        }

        // Set rgb[0:1] to color
        // Then buffer in
        GPIOB->ODR |= color;
        GPIOB->ODR |= 1 << 11;
        GPIOB->ODR &= ~(1 << 11);

        // Reset rgb[0:1] to all blank
        // And buffer into matrix until end of line
        GPIOB->ODR &= ~(0b111111);
        for (int i = curX + 1; i < 64; i++)
        {
            GPIOB->ODR |= 1 << 11;
            GPIOB->ODR &= ~(1 << 11);
        }

        // Set latch
        GPIOB->ODR &= ~(1 << 12);
    }

    // Enable line
    GPIOB->ODR &= ~(1 << 13);
}

//========================================================================
// Main Function
//========================================================================
int main(void)
{
    char event;
    char myString[] = {'x', '=', '0', '0', ' ', 'y', '=', '0', '0', '\0'};

    init_matrix();
    internal_clock();
    enable_ports();
    init_i2c();
    init_tim7();
    init_tim6();
    init_spi1();
    spi1_init_oled();
    enable_b();

    for (;;)
    {
        event = get_keypress();
        if (event == 'A' && curY < 63) // Increment Y
            curY++;
        else if (event == '3' && curY > 0) // Decrement Y
            curY--;
        else if (event == '1' && curX > 0) // Decrement X
            curX--;
        else if (event == '2' && curX < 63) // Increment X
            curX++;
        else if (event == 'C') // Change color
        {
            curColor++;
            if (curColor >= 7)
                curColor = 1;
        }
        else if (event == '#') // Reset drawing
            init_matrix();
        else if (event == 'D') // Save drawing
            save_drawing();
        else if (event == 'B') // Load drawing
            load_drawing();

        // Write to matrix
        write_matrix(curColor);

        // Display cursor position
        spi1_display1("CURSOR:");
        myString[3] = (char)(curX % 10 + '0');
        myString[2] = (char)(curX / 10 + '0');
        myString[8] = (char)(curY % 10 + '0');
        myString[7] = (char)(curY / 10 + '0');
        spi1_display2(myString);
    }
}
