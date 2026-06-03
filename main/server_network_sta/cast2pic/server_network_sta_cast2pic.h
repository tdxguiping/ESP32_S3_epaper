#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ServerNetworkStaCast2Pic_Process(httpd_req_t *req,
                                           const char *body,
                                           size_t body_len,
                                           const char *content_type,
                                           const char *base_path);

#ifdef __cplusplus
}
#endif


#if 0
$esp = "http://192.168.1.108"

$bin = "H:\AI2\1600-1200-img\4_color_800x480_96000.bin"
$image = "H:\AI2\1600-1200-img\jpg_img_1.jpg"

$binSize = (Get-Item $bin).Length
$imageSize = (Get-Item $image).Length

curl.exe -v "$esp/dataUP" `
  -F "func=cast2pic" `
  -F "screen=b" `
  -F "save=true" `
  -F "show=true" `
  -F "fileName=26422" `
  -F "bin_size=$binSize" `
  -F "image_size=$imageSize" `
  -F "bin=@$bin;type=application/octet-stream" `
  -F "image=@$image;type=image/jpeg"
#endif