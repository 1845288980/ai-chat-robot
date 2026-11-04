set(src_dirs
			iic
			led
			exti
			wifi_scan
			tcp_client
			inmp441)
			
set(include_dirs
			iic
			led
			exti
			wifi_scan
			tcp_client
			inmp441)

set(requires
			driver
            esp_driver_i2c
            esp_driver_i2s
            esp_event
            esp_netif
            esp_wifi
            nvs_flash)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
component_compile_options(-ffast-math -O3 -Wno-error=format -Wno-format)

