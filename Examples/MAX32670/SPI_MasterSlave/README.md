## Description

This example demonstrates a SPI transaction between two distinct SPI peripherals on the MAX32670. 

SPI1 is setup as the master in this example and is configured by default to send/receive 1024 8-bit words to and from the slave. Likewise, SPI0 is setup as the slave and is also expecting to both send and receive 1024 8-bit words to and from the master.

Once the master ends the transaction, the data received by the master and the slave is compared to the data sent by their counterpart to ensure all bytes were received properly.

## Required Connections

-   Connect a USB cable between the PC and the CN1 (USB/PWR) connector.
-   Select RX0 and TX0 on Headers JP1 and JP3 (UART 0).
-   Open an terminal application on the PC and connect to the EV kit's console UART at 115200, 8-N-1.
-   Close jumper JP1 (LED1 EN).
-   Close jumper JP2 (LED2 EN).
-   Connect the SPI pins on headers JH1 and JH3. (P0.2-->P0.14 (MISO), P0.3-->P0.15 (MOSI), P0.4-->P0.16 (SCK), and P0.5-->P0.17 (CS))

## Expected Output

The Console UART of the device will output these messages:

```
************************ SPI Master-Slave Example ************************
This example sends data between two SPI peripherals in the MAX32670.
SPI0 is configured as the slave and SPI1 is configured as the master.
Each SPI peripheral sends 1024 bytes on the SPI bus. If the data received
by each SPI instance matches the data sent by the other instance, the
green LED will illuminate, otherwise the red LED will illuminate.

Press SW3 to begin transaction.

EXAMPLE SUCCEEDED!
```