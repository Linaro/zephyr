Overview:
=========

As used by the 96boards Carbon board:

 \|/
  |
  |           [   Zephyr   ]           \       \       [   Zephyr   ]
[Radio] <---> [ controller ] <--XXXX---/ Break /--XX-> [  BLE HCI   ] <---> [Application]
              [   driver   ]           \       \       [   driver   ]
                    ^                                        ^
                    |                                        |
                    |                                        |
                    v                                        v
              [ Raw HCI driver ]                    [ Zephyr HCI stack ]
                    ^                                        ^
                    |                                        |
                    |                                        |
                    v                                        v
              [ This program ]                    [ BLE SPI Transport driver ]
                    |                                        |
                    |-------------- [ SPI Tunnel ] ----------|
                       (slave)                      (master)

   [Connectivity processor]                       [ Application Processor ]
   [      (e.g. nRF51)    ]                       [      (e.g. STM43F4)   ]
