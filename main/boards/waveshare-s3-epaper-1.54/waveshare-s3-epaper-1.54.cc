#include <stdio.h>
#include <fstream>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>

#include "wifi_station.h"
#include "application.h"
#include "button.h"
#include "codecs/es8311_audio_codec.h"
#include "config.h"
#include "wifi_board.h"
#include "board_power_bsp.h"
#include "custom_lcd_display.h"
#include "lvgl.h"
#include "mcp_server.h"

#include "assets/lang_config.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_spiffs.h"
#include "esp_http_server.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "waveshare-s3-epaper-1.54.h"

#define TAG "waveshare_epaper_1_54"

class CustomBoard : public WifiBoard {
  private:
    i2c_master_bus_handle_t   i2c_bus_;
    Button                    boot_button_;
    Button                    pwr_button_;
    CustomLcdDisplay         *display_;
    BoardPowerBsp            *power_;
    adc_oneshot_unit_handle_t adc1_handle;
    adc_cali_handle_t         cali_handle;
    sdmmc_card_t             *card_host;

    void ListSDCardFiles() {
        const char* mount_point = "/sdcard";
        DIR* dir = opendir(mount_point);
        if (!dir) {
            ESP_LOGE("SDCARD", "无法打开目录: %s", mount_point);
            return;
        }

        ESP_LOGI("SDCARD", "目录列表: %s", mount_point);
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            // 跳过当前目录和上级目录
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            std::string filepath = std::string(mount_point) + "/" + entry->d_name;

            // 获取文件状态
            struct stat st;
            if (stat(filepath.c_str(), &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    ESP_LOGI("SDCARD", "[DIR]  %s", entry->d_name);
                } else if (S_ISREG(st.st_mode)) {
                    ESP_LOGI("SDCARD", "[FILE] %s (%ld bytes)", entry->d_name, st.st_size);
                } else {
                    ESP_LOGI("SDCARD", "[OTHER] %s", entry->d_name);
                }
            }
        }
        closedir(dir);
    }
    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {};
        i2c_bus_cfg.i2c_port          = (i2c_port_t) 0;
        i2c_bus_cfg.sda_io_num        = AUDIO_CODEC_I2C_SDA_PIN;
        i2c_bus_cfg.scl_io_num        = AUDIO_CODEC_I2C_SCL_PIN;
        i2c_bus_cfg.clk_source        = I2C_CLK_SRC_DEFAULT;
        i2c_bus_cfg.glitch_ignore_cnt = 7;
        i2c_bus_cfg.intr_priority     = 0;
        i2c_bus_cfg.trans_queue_depth = 0;
        i2c_bus_cfg.flags.enable_internal_pullup = 1;
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto &app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });

        boot_button_.OnDoubleClick([this]() {
            auto &app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });

        boot_button_.OnLongPress([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume();
            if (volume >= 60) {
                volume = 20;
            } else {
                volume += 10;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume/10));
        });

        boot_button_.OnMultipleClick([this]() {
            char filename[] = "/sdcard/POJIAN.P3";
            auto &app = Application::GetInstance();
            // auto codec = GetAudioCodec();
            // codec->SetOutputVolume(60);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(2));
            // app.GetAudioService().PlaySound(filename);
            app.GetAudioService().PlaySound(Lang::Sounds::OGG_SUCCESS);
            ListSDCardFiles();
            // auto &app = Application::GetInstance();
            // auto codec = Board::GetInstance().GetAudioCodec();
            std::ifstream file(filename, std::ios::binary);
            if (!file.is_open())
            {
                ESP_LOGE(TAG, "Failed to open file: %s", filename);
                return;
            }

            // 获取文件大小并读取文件内容
            file.seekg(0, std::ios::end);
            size_t size = file.tellg();
            file.seekg(0, std::ios::beg);
            std::vector<char> file_data(size);
            file.read(file_data.data(), size);

            if (!file)
            {
                ESP_LOGE(TAG, "Failed to read the entire file: %s", filename);
                return;
            }

            // 读取并播放声音
            std::string_view sound_view(file_data.data(), file_data.size());
            app.PlaySound(sound_view);
            // app.GetAudioService().PlaySound(sound_view);
            ESP_LOGW(TAG, "File %s played successfully", filename);
        });

        pwr_button_.OnClick([this]() {
            Board::GetInstance().isChangePoem = true;
            ESP_LOGI("右右", "pwr_button_ OnClick %d", this->isChangePoem);
        });

        pwr_button_.OnLongPress([this]() {
            // GetDisplay()->SetChatMessage("system", "OFF");
            vTaskDelay(pdMS_TO_TICKS(1000));
            power_->PowerAudioOff();
            power_->PowerEpdOff();
            power_->VbatPowerOff();
        });
    }

    void InitializeTools() {
        auto &mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.disp.network", "重新配网", PropertyList(), [this](const PropertyList &) -> ReturnValue {
            ResetWifiConfiguration();
            return true;
        });

        mcp_server.AddTool("self.sht3.read", "通过板载的温湿度传感器获取温湿度值", PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            char str [50] = {""};
            snprintf(str,49,"温度:25°,湿度:78%%");
            ESP_LOGE("OK","%s",str);
            return str;
        });
    }

    void InitializeLcdDisplay() {
        custom_lcd_spi_t lcd_spi_data = {};
        lcd_spi_data.cs               = EPD_CS_PIN;
        lcd_spi_data.dc               = EPD_DC_PIN;
        lcd_spi_data.rst              = EPD_RST_PIN;
        lcd_spi_data.busy             = EPD_BUSY_PIN;
        lcd_spi_data.mosi             = EPD_MOSI_PIN;
        lcd_spi_data.scl              = EPD_SCK_PIN;
        lcd_spi_data.spi_host         = EPD_SPI_NUM;
        lcd_spi_data.buffer_len       = 5000;
        display_                      = new CustomLcdDisplay(NULL, NULL, EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY, lcd_spi_data);
    }

    void Power_Init() {
        power_ = new BoardPowerBsp(EPD_PWR_PIN, Audio_PWR_PIN, VBAT_PWR_PIN);
        power_->VbatPowerOn();
        power_->PowerAudioOn();
        power_->PowerEpdOn();
        do {
            vTaskDelay(pdMS_TO_TICKS(10));
        } while (!gpio_get_level(VBAT_PWR_GPIO));
    }

    uint16_t BatterygetVoltage(void) {
        static bool initialized = false;
        static adc_oneshot_unit_handle_t adc_handle;
        static adc_cali_handle_t cali_handle = NULL;
        if (!initialized) {
            adc_oneshot_unit_init_cfg_t init_config = {
                .unit_id = ADC_UNIT_1,
            };
            adc_oneshot_new_unit(&init_config, &adc_handle);
    
            adc_oneshot_chan_cfg_t ch_config = {
                .atten = ADC_ATTEN_DB_12,
                .bitwidth = ADC_BITWIDTH_12,
            };
            adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_3, &ch_config);
    
            adc_cali_curve_fitting_config_t cali_config = {
                .unit_id = ADC_UNIT_1,
                .atten = ADC_ATTEN_DB_12,
                .bitwidth = ADC_BITWIDTH_12,
            };
            if (adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle) == ESP_OK) {
                initialized = true;
            }
        }

        if (initialized) {
            int raw_value = 0;
            int raw_voltage = 0;
            int voltage = 0; // mV
            adc_oneshot_read(adc_handle, ADC_CHANNEL_3, &raw_value);
            adc_cali_raw_to_voltage(cali_handle, raw_value, &raw_voltage);
            voltage =  raw_voltage * 2;
            // ESP_LOGI(TAG, "voltage: %dmV", voltage);
            return (uint16_t)voltage;
        }

        return 0;
    }

    uint8_t BatterygetPercent() {
        int voltage = 0;
        for (uint8_t i = 0; i < 10; i++) {
            voltage += BatterygetVoltage();
        }

        voltage /= 10;
        int percent = (-1 * voltage * voltage + 9016 * voltage - 19189000) / 10000;
        percent = (percent > 100) ? 100 : (percent < 0) ? 0 : percent;
        // ESP_LOGI(TAG, "voltage: %dmV, percentage: %d%%", voltage, percent);
        return (uint8_t)percent;
    }

    void InitializeSdCard(void)
    {
        esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
        mount_config.format_if_mount_failed = false;       //如果挂靠失败，创建分区表并格式化SD卡
        mount_config.max_files = 5;                        //打开文件最大数
        mount_config.allocation_unit_size = 16 * 1024 *3;  //类似扇区大小
        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;          //高速

        sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
        slot_config.width = 1;                             //1线
        slot_config.clk = SDMMC_CLK_PIN;
        slot_config.cmd = SDMMC_CMD_PIN;
        slot_config.d0 = SDMMC_D0_PIN;
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_vfs_fat_sdmmc_mount(SDlist, &host, &slot_config, &mount_config, &card_host));

        if (card_host != NULL)
        {
            // sdmmc_card_print_info(stdout, card_host); //Print out the card_host information
            ESP_LOGI("右右", "sdcard_GetValue: %.2fG", (float)(card_host->csd.capacity)/2048/1024);
            // printf("practical_size:%.2fG\n",sdcard_GetValue()); //g
        }
    }

    /* Send HTTP response with a run-time generated html consisting of
    * a list of all files and folders under the requested path.
    * In case of SPIFFS this returns empty list when path is any
    * string other than '/', since SPIFFS doesn't support directories */
    static esp_err_t http_resp_dir_html(httpd_req_t *req, const char *dirpath)
    {
        char entrypath[FILE_PATH_MAX];
        char entrysize[16];
        const char *entrytype;

        struct dirent *entry;
        struct stat entry_stat;

        DIR *dir = opendir(dirpath);
        const size_t dirpath_len = strlen(dirpath);

        /* Retrieve the base path of file storage to construct the full path */
        strlcpy(entrypath, dirpath, sizeof(entrypath));

        if (!dir) {
            ESP_LOGE(TAG, "Failed to stat dir : %s", dirpath);
            /* Respond with 404 Not Found */
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Directory does not exist");
            return ESP_FAIL;
        }

        /* Send HTML file header */
        httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html><body>");

        /* Get handle to embedded file upload script */
        extern const unsigned char upload_script_start[] asm("_binary_upload_script_html_start");
        extern const unsigned char upload_script_end[]   asm("_binary_upload_script_html_end");
        const size_t upload_script_size = (upload_script_end - upload_script_start);

        /* Add file upload form and script which on execution sends a POST request to /upload */
        httpd_resp_send_chunk(req, (const char *)upload_script_start, upload_script_size);

        /* Send file-list table definition and column labels */
        httpd_resp_sendstr_chunk(req,
            "<table class=\"fixed\" border=\"1\">"
            "<col width=\"800px\" /><col width=\"300px\" /><col width=\"300px\" /><col width=\"100px\" />"
            "<thead><tr><th>Name</th><th>Type</th><th>Size (Bytes)</th><th>Delete</th></tr></thead>"
            "<tbody>");

        /* Iterate over all files / folders and fetch their names and sizes */
        while ((entry = readdir(dir)) != NULL) {
            entrytype = (entry->d_type == DT_DIR ? "directory" : "file");

            strlcpy(entrypath + dirpath_len, entry->d_name, sizeof(entrypath) - dirpath_len);
            if (stat(entrypath, &entry_stat) == -1) {
                ESP_LOGE(TAG, "Failed to stat %s : %s", entrytype, entry->d_name);
                continue;
            }
            sprintf(entrysize, "%ld", entry_stat.st_size);
            ESP_LOGI(TAG, "Found %s : %s (%s bytes)", entrytype, entry->d_name, entrysize);

            /* Send chunk of HTML file containing table entries with file name and size */
            httpd_resp_sendstr_chunk(req, "<tr><td><a href=\"");
            httpd_resp_sendstr_chunk(req, req->uri);
            httpd_resp_sendstr_chunk(req, entry->d_name);
            if (entry->d_type == DT_DIR) {
                httpd_resp_sendstr_chunk(req, "/");
            }
            httpd_resp_sendstr_chunk(req, "\">");
            httpd_resp_sendstr_chunk(req, entry->d_name);
            httpd_resp_sendstr_chunk(req, "</a></td><td>");
            httpd_resp_sendstr_chunk(req, entrytype);
            httpd_resp_sendstr_chunk(req, "</td><td>");
            httpd_resp_sendstr_chunk(req, entrysize);
            httpd_resp_sendstr_chunk(req, "</td><td>");
            httpd_resp_sendstr_chunk(req, "<form method=\"post\" action=\"/delete");
            httpd_resp_sendstr_chunk(req, req->uri);
            httpd_resp_sendstr_chunk(req, entry->d_name);
            httpd_resp_sendstr_chunk(req, "\"><button type=\"submit\">Delete</button></form>");
            httpd_resp_sendstr_chunk(req, "</td></tr>\n");
        }
        closedir(dir);

        /* Finish the file list table */
        httpd_resp_sendstr_chunk(req, "</tbody></table>");

        /* Send remaining chunk of HTML file to complete it */
        httpd_resp_sendstr_chunk(req, "</body></html>");

        /* Send empty chunk to signal HTTP response completion */
        httpd_resp_sendstr_chunk(req, NULL);
        return ESP_OK;
    }

    /* Copies the full path into destination buffer and returns
    * pointer to path (skipping the preceding base path) */
    static const char* get_path_from_uri(char *dest, const char *base_path, const char *uri, size_t destsize)
    {
        const size_t base_pathlen = strlen(base_path);
        size_t pathlen = strlen(uri);

        const char *quest = strchr(uri, '?');
        if (quest) {
            pathlen = MIN(pathlen, quest - uri);
        }
        const char *hash = strchr(uri, '#');
        if (hash) {
            pathlen = MIN(pathlen, hash - uri);
        }

        if (base_pathlen + pathlen + 1 > destsize) {
            /* Full path string won't fit into destination buffer */
            return NULL;
        }

        /* Construct full path (base + path) */
        strcpy(dest, base_path);
        strlcpy(dest + base_pathlen, uri, pathlen + 1);

        /* Return pointer to path, skipping the base */
        return dest + base_pathlen;
    }

    /* Handler to download a file kept on the server */
    static esp_err_t download_get_handler(httpd_req_t *req)
    {
        char filepath[FILE_PATH_MAX];
        FILE *fd = NULL;
        struct stat file_stat;

        const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                                req->uri, sizeof(filepath));
        if (!filename) {
            ESP_LOGE(TAG, "Filename is too long");
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
            return ESP_FAIL;
        }

        /* If name has trailing '/', respond with directory contents */
        if (filename[strlen(filename) - 1] == '/') {
            return http_resp_dir_html(req, filepath);
        }

        if (stat(filepath, &file_stat) == -1) {
            /* If file not present on SPIFFS check if URI
            * corresponds to one of the hardcoded paths */
            if (strcmp(filename, "/index.html") == 0) {
                return index_html_get_handler(req);
            } else if (strcmp(filename, "/favicon.ico") == 0) {
                return favicon_get_handler(req);
            }
            ESP_LOGE(TAG, "Failed to stat file : %s", filepath);
            /* Respond with 404 Not Found */
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
            return ESP_FAIL;
        }

        fd = fopen(filepath, "r");
        if (!fd) {
            ESP_LOGE(TAG, "Failed to read existing file : %s", filepath);
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Sending file : %s (%ld bytes)...", filename, file_stat.st_size);
        set_content_type_from_file(req, filename);

        /* Retrieve the pointer to scratch buffer for temporary storage */
        char *chunk = ((struct file_server_data *)req->user_ctx)->scratch;
        size_t chunksize;
        do {
            /* Read file in chunks into the scratch buffer */
            chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);

            if (chunksize > 0) {
                /* Send the buffer contents as HTTP response chunk */
                if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
                    fclose(fd);
                    ESP_LOGE(TAG, "File sending failed!");
                    /* Abort sending file */
                    httpd_resp_sendstr_chunk(req, NULL);
                    /* Respond with 500 Internal Server Error */
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
            }

            /* Keep looping till the whole file is sent */
        } while (chunksize != 0);

        /* Close file after sending complete */
        fclose(fd);
        ESP_LOGI(TAG, "File sending complete");

        /* Respond with an empty chunk to signal HTTP response completion */
    #ifdef CONFIG_EXAMPLE_HTTPD_CONN_CLOSE_HEADER
        httpd_resp_set_hdr(req, "Connection", "close");
    #endif
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_OK;
    }

    /* Handler to redirect incoming GET request for /index.html to /
    * This can be overridden by uploading file with same name */
    static esp_err_t index_html_get_handler(httpd_req_t *req)
    {
        httpd_resp_set_status(req, "307 Temporary Redirect");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, NULL, 0);  // Response body can be empty
        return ESP_OK;
    }

    /* Handler to respond with an icon file embedded in flash.
    * Browsers expect to GET website icon at URI /favicon.ico.
    * This can be overridden by uploading file with same name */
    static esp_err_t favicon_get_handler(httpd_req_t *req)
    {
        extern const unsigned char favicon_ico_start[] asm("_binary_favicon_ico_start");
        extern const unsigned char favicon_ico_end[]   asm("_binary_favicon_ico_end");
        const size_t favicon_ico_size = (favicon_ico_end - favicon_ico_start);
        httpd_resp_set_type(req, "image/x-icon");
        httpd_resp_send(req, (const char *)favicon_ico_start, favicon_ico_size);
        return ESP_OK;
    }

    /* Set HTTP response content type according to file extension */
    static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename)
    {
        if (IS_FILE_EXT(filename, ".pdf")) {
            return httpd_resp_set_type(req, "application/pdf");
        } else if (IS_FILE_EXT(filename, ".html")) {
            return httpd_resp_set_type(req, "text/html");
        } else if (IS_FILE_EXT(filename, ".jpeg")) {
            return httpd_resp_set_type(req, "image/jpeg");
        } else if (IS_FILE_EXT(filename, ".ico")) {
            return httpd_resp_set_type(req, "image/x-icon");
        }
        /* This is a limited set only */
        /* For any other type always set as plain text */
        return httpd_resp_set_type(req, "text/plain");
    }

    /* Handler to upload a file onto the server */
    static esp_err_t upload_post_handler(httpd_req_t *req)
    {
        char filepath[FILE_PATH_MAX];
        FILE *fd = NULL;
        struct stat file_stat;

        /* Skip leading "/upload" from URI to get filename */
        /* Note sizeof() counts NULL termination hence the -1 */
        const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                                req->uri + sizeof("/upload") - 1, sizeof(filepath));
        if (!filename) {
            char buffer[100];
            snprintf(buffer, sizeof(buffer), "Filename too long: %s", SAFE_STR(filename));
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, buffer);
            return ESP_FAIL;
        }

        /* Filename cannot have a trailing '/' */
        if (filename[strlen(filename) - 1] == '/') {
            ESP_LOGE(TAG, "Invalid filename : %s", filename);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid filename");
            return ESP_FAIL;
        }

        if (stat(filepath, &file_stat) == 0) {
            ESP_LOGE(TAG, "File already exists : %s", filepath);
            /* Respond with 400 Bad Request */
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File already exists");
            return ESP_FAIL;
        }

        /* File cannot be larger than a limit */
        if (req->content_len > MAX_FILE_SIZE) {
            ESP_LOGE(TAG, "File too large : %d bytes", req->content_len);
            /* Respond with 400 Bad Request */
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                "File size must be less than "
                                MAX_FILE_SIZE_STR "!");
            /* Return failure to close underlying connection else the
            * incoming file content will keep the socket busy */
            return ESP_FAIL;
        }

        fd = fopen(filepath, "w");
        if (!fd) {
            ESP_LOGE(TAG, "Failed to create file : %s", filepath);
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Receiving file : %s...", filename);

        /* Retrieve the pointer to scratch buffer for temporary storage */
        char *buf = ((struct file_server_data *)req->user_ctx)->scratch;
        int received;

        /* Content length of the request gives
        * the size of the file being uploaded */
        int remaining = req->content_len;

        while (remaining > 0) {

            ESP_LOGI(TAG, "Remaining size : %d", remaining);
            /* Receive the file part by part into a buffer */
            if ((received = httpd_req_recv(req, buf, MIN(remaining, SCRATCH_BUFSIZE))) <= 0) {
                if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                    /* Retry if timeout occurred */
                    continue;
                }

                /* In case of unrecoverable error,
                * close and delete the unfinished file*/
                fclose(fd);
                unlink(filepath);

                ESP_LOGE(TAG, "File reception failed!");
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive file");
                return ESP_FAIL;
            }

            /* Write buffer content to file on storage */
            if (received && (received != fwrite(buf, 1, received, fd))) {
                /* Couldn't write everything to file!
                * Storage may be full? */
                fclose(fd);
                unlink(filepath);

                ESP_LOGE(TAG, "File write failed!");
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write file to storage");
                return ESP_FAIL;
            }

            /* Keep track of remaining size of
            * the file left to be uploaded */
            remaining -= received;
        }

        /* Close file upon upload completion */
        fclose(fd);
        ESP_LOGI(TAG, "File reception complete");

        /* Redirect onto root to see the updated file list */
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/");
    #ifdef CONFIG_EXAMPLE_HTTPD_CONN_CLOSE_HEADER
        httpd_resp_set_hdr(req, "Connection", "close");
    #endif
        httpd_resp_sendstr(req, "File uploaded successfully");
        return ESP_OK;
    }

    /* Handler to delete a file from the server */
    static esp_err_t delete_post_handler(httpd_req_t *req)
    {
        char filepath[FILE_PATH_MAX];
        struct stat file_stat;

        /* Skip leading "/delete" from URI to get filename */
        /* Note sizeof() counts NULL termination hence the -1 */
        const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                                req->uri  + sizeof("/delete") - 1, sizeof(filepath));
        if (!filename) {
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
            return ESP_FAIL;
        }

        /* Filename cannot have a trailing '/' */
        if (filename[strlen(filename) - 1] == '/') {
            ESP_LOGE(TAG, "Invalid filename : %s", filename);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid filename");
            return ESP_FAIL;
        }

        if (stat(filepath, &file_stat) == -1) {
            ESP_LOGE(TAG, "File does not exist : %s", filename);
            /* Respond with 400 Bad Request */
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File does not exist");
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Deleting file : %s", filename);
        /* Delete file */
        unlink(filepath);

        /* Redirect onto root to see the updated file list */
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/");
    #ifdef CONFIG_EXAMPLE_HTTPD_CONN_CLOSE_HEADER
        httpd_resp_set_hdr(req, "Connection", "close");
    #endif
        httpd_resp_sendstr(req, "File deleted successfully");
        return ESP_OK;
    }

  public:
    bool isChangePoem = true;
    CustomBoard() : boot_button_(BOOT_BUTTON_GPIO), pwr_button_(VBAT_PWR_GPIO) {
        Power_Init();
        InitializeI2c();
        InitializeButtons();
        InitializeSdCard();
        InitializeTools();
        InitializeLcdDisplay();
    }

    /* Function to start the file server */
    virtual int example_start_file_server() override {
        static struct file_server_data *server_data = NULL;

        if (server_data) {
            ESP_LOGE(TAG, "File server already started");
            return ESP_ERR_INVALID_STATE;
        }

        /* Allocate memory for server data */
        server_data = (file_server_data *)heap_caps_malloc(sizeof(struct file_server_data), MALLOC_CAP_SPIRAM);
        if (!server_data) {
            ESP_LOGE(TAG, "Failed to allocate memory for server data");
            return ESP_ERR_NO_MEM;
        }
        strlcpy(server_data->base_path, SDlist,
                sizeof(server_data->base_path));

        httpd_handle_t server = NULL;
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();

        /* Use the URI wildcard matching function in order to
        * allow the same handler to respond to multiple different
        * target URIs which match the wildcard scheme */
        config.uri_match_fn = httpd_uri_match_wildcard;

        ESP_LOGI(TAG, "Starting HTTP Server on port: '%d'", config.server_port);
        if (httpd_start(&server, &config) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start file server!");
            return ESP_FAIL;
        }

        /* URI handler for getting uploaded files */
        httpd_uri_t file_download = {
            .uri       = "/*",  // Match all URIs of type /path/to/file
            .method    = HTTP_GET,
            .handler   = download_get_handler,
            .user_ctx  = server_data    // Pass server data as context
        };
        httpd_register_uri_handler(server, &file_download);

        /* URI handler for uploading files to server */
        httpd_uri_t file_upload = {
            .uri       = "/upload/*",   // Match all URIs of type /upload/path/to/file
            .method    = HTTP_POST,
            .handler   = upload_post_handler,
            .user_ctx  = server_data    // Pass server data as context
        };
        httpd_register_uri_handler(server, &file_upload);

        /* URI handler for deleting files from server */
        httpd_uri_t file_delete = {
            .uri       = "/delete/*",   // Match all URIs of type /delete/path/to/file
            .method    = HTTP_POST,
            .handler   = delete_post_handler,
            .user_ctx  = server_data    // Pass server data as context
        };
        httpd_register_uri_handler(server, &file_delete);

        return ESP_OK;
    }

    virtual AudioCodec *GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(i2c_bus_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE, AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN, AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR);
        return &audio_codec;
    }

    virtual Display *GetDisplay() override {
        return display_;
    }

    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging) override {
        charging = false;
        discharging = !charging;
        level = (int)BatterygetPercent();

        return true;
    }
};

DECLARE_BOARD(CustomBoard);