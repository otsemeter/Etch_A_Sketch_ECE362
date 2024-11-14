# Etch_A_Sketch_ECE362
Group Project for ECE 36200 group 85

Project Description: We want to make an advanced version of Etch-A-Sketch by utilizing an LED grid as a drawing surface by moving a cursor with keypad controls. The user will be able to save, reload, edit, and delete designs, as well as change the cursor color which will be displayed on the 3 pronged LED. Sketches will each be assigned an ID to be stored in memory - along with the matrix representation of the design - which will be shown on the TFT LCD display.

Objectives:
An ability to “draw” on an LED matrix by bit-banging its protocol
Have a tracked “cursor” moved on LED matrix via keypad along the X and Y axis
An ability to change the color of the “cursor” using PWM, and to “shake” the grid clear of the current sketch
An ability to save the current sketch and to load and delete saved sketches with 24AA32AF EEPROM using I2C protocol



Main Features: Features:
Move cursor over led grid using keypad,
select led colors using keypad, and display selected color on 3 pronged LED
Save current screen to flash memory along with drawing ID
Access and scroll through old drawings
Delete saved drawings with a double confirmation

Uses:
 LED grid (https://nam04.safelinks.protection.outlook.com/?url=https%3A%2F%2Fwww.adafruit.com%2Fproduct%2F1484&data=05%7C02%7Cosemeter%40purdue.edu%7C8312a72d11a1469fd8c308dced77d88b%7C4130bd397c53419cb1e58758d6d63f21%7C0%7C0%7C638646346229228253%7CUnknown%7CTWFpbGZsb3d8eyJWIjoiMC4wLjAwMDAiLCJQIjoiV2luMzIiLCJBTiI6Ik1haWwiLCJXVCI6Mn0%3D%7C0%7C%7C%7C&sdata=%2B4Yr1eSQv03UgNae9EbAQ7DtdLH9rHRn8S7rVQ1MDj4%3D&reserved=0)
RGB LED
Keypad
LCD display

Code:
Use I2C protocol to write and read new and old drawings to and from flash memory respectively
Use SPI to display the ID of the sketch/when it was made on the LCD display
Use pulse width modulation to control the color of the cursor on the LED grid as well as the color displayed on the RGB LED
Use button as GPIO for whether or not to save/delete a sketch



External Interfaces: I2C
GPIO
SPI
PWM


Internal Peripherals: GPIO
Internal Timers
SPI
I2C
ADC
DMA


Timeline: October 18, 2024: order parts, complete I2C lab to learn about non-volatile memory
October 25, 2024: complete initialization code for GPIOC, LCD display, RABG LED, and timers, and bit bang pins needed for LED matrix
November 1, 2024: Implement color changing using PWM for LED matrix and RABG LED, implement function to move “cursor” over LED matrix using keypad, implement function to reset cursor position and clear LED matrix, and implement function to Display sketch ID on LCD display using SPI
November 8, 2024: Complete I2C initialization code, and implement sketch save by writing the matrix representation of the current sketch to 24AA32AF EEPROM. Implement function to scroll between saved matrices in memory using keypad, and display current ID on the LCD display. Implement function to delete current matrix from memory.
November 15, 2024: Implement function to read matrix from 24AA32AF EEPROM. Implement function to write matrix representation to LED matrix. Implement load function that upon a button press would allow the currently displayed sketch to be edited.



Related Projects: Piece of Paper and Pencil:
https://nam04.safelinks.protection.outlook.com/?url=https%3A%2F%2Fminiatures.com%2F2-white-legal-pads-and-pencil%2F%3FsetCurrencyId%3D1%26sku%3D56103&data=05%7C02%7Cosemeter%40purdue.edu%7C8312a72d11a1469fd8c308dced77d88b%7C4130bd397c53419cb1e58758d6d63f21%7C0%7C0%7C638646346229269777%7CUnknown%7CTWFpbGZsb3d8eyJWIjoiMC4wLjAwMDAiLCJQIjoiV2luMzIiLCJBTiI6Ik1haWwiLCJXVCI6Mn0%3D%7C0%7C%7C%7C&sdata=qG8mT60wEPX7w37ILNlJf%2Fsi54wvBLEx1GGAC1%2Bif4M%3D&reserved=0

Our design would be an improvement upon this product because it allows for an array of different colors, as well as quick smudge free editing of sketches.

Web-Based Etch-a-Sketch using Virtual Canvas Project
(explanation: https://nam04.safelinks.protection.outlook.com/?url=https%3A%2F%2Fwww.reddit.com%2Fr%2Ftheodinproject%2Fcomments%2F1de4vxr%2Fetchasketch%2F&data=05%7C02%7Cosemeter%40purdue.edu%7C8312a72d11a1469fd8c308dced77d88b%7C4130bd397c53419cb1e58758d6d63f21%7C0%7C0%7C638646346229298943%7CUnknown%7CTWFpbGZsb3d8eyJWIjoiMC4wLjAwMDAiLCJQIjoiV2luMzIiLCJBTiI6Ik1haWwiLCJXVCI6Mn0%3D%7C0%7C%7C%7C&sdata=AJnBVIAJfUe408iE9KAz0cz0d0fcHBl5zRnfMLWGBxw%3D&reserved=0
website: https://nam04.safelinks.protection.outlook.com/?url=http%3A%2F%2Fsigmadev.me%2FProject-Etch-a-Sketch-%2Findex.html&data=05%7C02%7Cosemeter%40purdue.edu%7C8312a72d11a1469fd8c308dced77d88b%7C4130bd397c53419cb1e58758d6d63f21%7C0%7C0%7C638646346229319227%7CUnknown%7CTWFpbGZsb3d8eyJWIjoiMC4wLjAwMDAiLCJQIjoiV2luMzIiLCJBTiI6Ik1haWwiLCJXVCI6Mn0%3D%7C0%7C%7C%7C&sdata=AAZs6pLb3v6cO7Q1wsI6TcDNCKjsK0XqMZqyBOLbs44%3D&reserved=0)

Our design differs from this by allowing for designs/drawings to be saved, reloaded and further edited, along with the possible implementations of bringing the “pen up” and “pen down”.
