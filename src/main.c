#include "stm32f0xx.h"

int matrix[64][64]; //global matrix holding current board configuration
int curX = 0; //cursor x position
int curY = 0; //cursor y position
int curColor = 1; //current color defaulted to red
int dispRow = 0; //current row being displayed, defaulted to row 0
void set_char_msg(int, char);
void nano_wait(unsigned int);
void internal_clock();
void check_wiring();
void display_row(int row); //display selected row on matrix
void display_cursor(); //display cursor on matrix

//===========================================================================
// Configure GPIOC
//===========================================================================
void enable_ports(void)
{
    // Only enable port C for the keypad
    RCC->AHBENR |= RCC_AHBENR_GPIOCEN;
    GPIOC->MODER &= ~0xffff;
    GPIOC->MODER |= 0x55 << (4 * 2);
    GPIOC->OTYPER &= ~0xff;
    GPIOC->OTYPER |= 0xf0;
    GPIOC->PUPDR &= ~0xff;
    GPIOC->PUPDR |= 0x55;
}

/*
configure GPIOB
*/
void enable_b()
{
    RCC->AHBENR |= RCC_AHBENR_GPIOBEN;
    GPIOB->MODER &= ~0xFFFFFFF;
    GPIOB->MODER |= 0x5555555;
    // GPIOB->PUPDR &= ~0xFFFFFFF;
    // GPIOB->PUPDR |= 0x5555555;
}


uint8_t col; // the column being scanned

void drive_column(int);                 // energize one of the column outputs
int read_rows();                        // read the four row inputs
void update_history(int col, int rows); // record the buttons of the driven column
char get_key_event(void);               // wait for a button event (press or release)
char get_keypress(void);                // wait for only a button press event.
float getfloat(void);                   // read a floating-point number from keypad
void show_keys(void);                   // demonstrate get_key_event()

//===========================================================================
// Bit Bang SPI LED Array
//===========================================================================
int msg_index = 0;
uint16_t msg[8] = {0x0000, 0x0100, 0x0200, 0x0300, 0x0400, 0x0500, 0x0600, 0x0700};
extern const char font[];

//============================================================================
// Configure Timer 15 for an update rate of 1 kHz.
// Trigger the DMA channel on each update.
// Copy this from lab 4 or lab 5.
//============================================================================
void init_tim15(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_TIM15EN;
    TIM15->PSC = 4800 - 1;
    TIM15->ARR = 10 - 1;
    TIM15->DIER |= TIM_DIER_UDE;
    TIM15->CR1 |= TIM_CR1_CEN;
}

//===========================================================================
// Configure timer 7 to invoke the update interrupt at 1kHz
//===========================================================================
void init_tim7(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM7EN;
    TIM7->PSC = 4800 - 1;
    TIM7->ARR = 10 - 1;
    TIM7->DIER |= TIM_DIER_UIE;
    NVIC->ISER[0] = 1 << TIM7_IRQn;
    TIM7->CR1 |= TIM_CR1_CEN;
}

/*
Configure timer 6 to invoke the update interrupt at 5.56 kHz (found through experimentation)
*/
void init_tim6(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM6EN;
    TIM6->PSC = 480 - 1;
    TIM6->ARR = 18 - 1;
    TIM6->DIER |= TIM_DIER_UIE;
    NVIC->ISER[0] = 1 << TIM6_IRQn;
    TIM6->CR1 |= TIM_CR1_CEN;
}

//===========================================================================
// Copy the Timer 7 ISR from lab 5 and added a call to cursor display
//===========================================================================
void TIM7_IRQHandler()
{
    TIM7->SR &= ~TIM_SR_UIF;
    int rows = read_rows();
    update_history(col, rows);
    col = (col + 1) & 3;
    drive_column(col);
    display_cursor();
}

/*
Timer 6 ISR that calls display board on the global variable dispRow
and then itterates it, resetting it to 0 when it reaches the last row.
ISR goes one row at a time instead of the whole board because it was unable to finish
its full loop through all rows before another interrupt was thrown, therefore never
displaying the board
*/
void TIM6_IRQHandler()
{
    TIM6->SR &= ~TIM_SR_UIF;

    display_row(dispRow++);
    if (dispRow >= 64)
        dispRow = 0;
}

//===========================================================================
// Initialize the SPI2 peripheral.
//===========================================================================
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

//===========================================================================
// Configure the SPI2 peripheral to trigger the DMA channel when the
// transmitter is empty.  Use the code from setup_dma from lab 5.
//===========================================================================
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

//===========================================================================
// Enable the DMA channel.
//===========================================================================
void spi2_enable_dma(void)
{
    DMA1_Channel5->CCR |= DMA_CCR_EN;
}

//===========================================================================
// 4.4 SPI OLED Display
//===========================================================================
void init_spi1()
{
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
void spi1_init_oled()
{
    nano_wait(1000000); // wait 1 ms
    spi_cmd(0x38);      // fuction set (idk what that means)
    small_delay();
    spi_cmd(0x08); // turn display off
    small_delay();
    spi_cmd(0x01);      // clear display
    nano_wait(2000000); // wait 2 ms
    spi_cmd(0x06);      // set entry mode
    small_delay();
    spi_cmd(0x02); // move cursor to home position
    small_delay();
    spi_cmd(0x0c); // turn on display
}
void spi1_display1(const char *string)
{
    spi_cmd(0x02); // move cursor to home position
    int idx = 0;
    while (string[idx] != '\0')
    {
        spi_data(string[idx++]);
    }
}
void spi1_display2(const char *string)
{
    spi_cmd(0xc0);
    int idx = 0;
    while (string[idx] != '\0')
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
    0x200 + 'E',
    0x200 + 'C',
    0x200 + 'E',
    0x200 + '3',
    0x200 + '6',
    +0x200 + '2',
    0x200 + ' ',
    0x200 + 'i',
    0x200 + 's',
    0x200 + ' ',
    0x200 + 't',
    0x200 + 'h',
    +0x200 + 'e',
    0x200 + ' ',
    0x200 + ' ',
    0x200 + ' ',
    0x0c0, // Command to set the cursor at the first position line 2
    0x200 + 'c',
    0x200 + 'l',
    0x200 + 'a',
    0x200 + 's',
    0x200 + 's',
    +0x200 + ' ',
    0x200 + 'f',
    0x200 + 'o',
    0x200 + 'r',
    0x200 + ' ',
    0x200 + 'y',
    0x200 + 'o',
    +0x200 + 'u',
    0x200 + '!',
    0x200 + ' ',
    0x200 + ' ',
};

//===========================================================================
// Configure the proper DMA channel to be triggered by SPI1_TX.
// Set the SPI1 peripheral to trigger a DMA when the transmitter is empty.
//===========================================================================
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

//===========================================================================
// Enable the DMA channel triggered by SPI1_TX.
//===========================================================================
void spi1_enable_dma(void)
{

    DMA1_Channel3->CCR |= DMA_CCR_EN;
}

// set 3-bit RGB value at cursor location in matrix
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
//===========================================================================
// Main function
//===========================================================================

int main(void)
{
    char event;
    char myString[] = {'x', '=', '0', '0', ' ', 'y', '=', '0', '0', '\0'};
    init_matrix();
    internal_clock();
    enable_ports();
    init_tim7();
    init_tim6();
    init_spi1();
    spi1_init_oled();
    enable_b();

    for (;;)
    {
        event = get_keypress();
        if (event == 'A' && curY < 63) //increment y
            curY++;
        else if (event == '3' && curY > 0) //decrement Y
            curY--;
        else if (event == '1' && curX > 0) //decrement X
            curX--;
        else if (event == '2' && curX < 63) //increment X
            curX++;
        else if (event == 'C') //increment color, resetting at 7 so that white is reserved for cursor
        {
            curColor++;
            if (curColor >= 7)
                curColor = 1;
        }
        else if (event == '#') //reset drawing (potentially also save, but maybe have save and reset separate)
            init_matrix();

        //call to write matrix after every key event
        write_matrix(curColor);

        //display cursor location on tft display, maybe change to drawing title?
        spi1_display1("CURSOR:");
        myString[3] = (char)(curX % 10 + 48);
        myString[2] = (char)(curX / 10 + 48);
        myString[8] = (char)(curY % 10 + 48);
        myString[7] = (char)(curY / 10 + 48);
        spi1_display2(myString);
    }
}
