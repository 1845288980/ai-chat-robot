set(src_dirs
			iic
			led
			exti
			wifi_scan
			tcp_client)
			
set(include_dirs
			iic
			led
			exti
			wifi_scan
			tcp_client)

set(requires
			driver
            esp_driver_i2c
            esp_event
            esp_netif
            esp_wifi
            nvs_flash)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
component_compile_options(-ffast-math -O3 -Wno-error=format -Wno-format)

