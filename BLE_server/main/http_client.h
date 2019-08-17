#ifdef __cplusplus
extern "C" {
#endif


typedef struct {
    uint8_t* response;
    size_t size;
} ota_file_t;

ota_file_t* get_ota_file();
void http_client(void*);
uint8_t* get_ota_version();

#ifdef __cplusplus
}
#endif