obj-$(CONFIG_SPI_INTEL) += spi_intel.o
obj-$(CONFIG_SPI_DW) += spi_dw.o
obj-$(CONFIG_SPI_MCUX) += spi_mcux.o
obj-$(CONFIG_SPI_QMSI) += spi_qmsi.o
obj-$(CONFIG_SPI_QMSI_SS) += spi_qmsi_ss.o
obj-$(CONFIG_SPI_STM32) += spi_stm32.o
ifeq ($(CONFIG_SPI_SLAVE),y)
	obj-$(CONFIG_SPI_NRF5) += spi_nrf5_slave.o
endif
