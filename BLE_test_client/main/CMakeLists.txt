idf_component_register(SRCS "main.cpp" "main_ble.cpp" "http_client.c" "ota_write.c"
                    INCLUDE_DIRS ""
                    EMBED_TXTFILES ssl_root_cert.pem)