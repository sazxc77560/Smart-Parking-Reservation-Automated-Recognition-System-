#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#define ALARM_DEVICE "/dev/etx_alarm"
// ----------------------------------------------------
// 系統與網路設定
// ----------------------------------------------------
#define SERVER_IP "172.21.11.211" // Main Control Server IP
#define SERVER_PORT 8888
#define BUFFER_SIZE 1024
#define MONITOR_SPOT "A1"           // 監控指定的車位 ID
#define PIPE_PATH "/tmp/lpr_pipe"   // 定義 Pipe 的路徑

// ----------------------------------------------------
// 函式原型宣告
// ----------------------------------------------------
int request_spot_status(const char *spot_id, char *response_buffer);
void trigger_alarm(const char *message);

// ----------------------------------------------------
// 輔助函式 (模擬警報驅動)
// ----------------------------------------------------

void trigger_alarm(const char *message) {
    // 1. 印出 Log
    printf("\n====================================\n");
    printf("!!! 🚨 ALARM 🚨 !!!\n");
    printf("車位 [%s] 狀態異常: %s\n", MONITOR_SPOT, message);
    printf("====================================\n");

    // 2. 開啟警報裝置 (ALARM_DEVICE 必須在上面定義過)
    int fd = open(ALARM_DEVICE, O_WRONLY);
    if (fd < 0) {
        perror("⚠️ 無法開啟警報裝置"); 
        return;
    }
    
    // 3. 寫入 '5' 啟動 5 秒倒數
    write(fd, "5", 1); 
    
    // 4. 關閉檔案
    close(fd);
}

// ----------------------------------------------------
// Socket 請求函式
// ----------------------------------------------------

/**
 * @brief 向 Server 請求指定車位的狀態 (GET_SPOT_PLATE)
 * @return 0 成功，-1 失敗
 */
int request_spot_status(const char *spot_id, char *response_buffer) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char request[BUFFER_SIZE];
    
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "❌ Socket 建立失敗\n");
        return -1;
    }
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "❌ 無效的 Server IP 地址\n");
        close(sock);
        return -1;
    }
    
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("❌ 連線 Server 失敗");
        close(sock);
        return -1;
    }

    // 格式化請求: GET_SPOT_PLATE|<車位ID>
    sprintf(request, "GET_SPOT_PLATE|%s", spot_id);
    send(sock, request, strlen(request), 0);
    
    int valread = read(sock, response_buffer, BUFFER_SIZE - 1);
    close(sock);

    if (valread > 0) {
        response_buffer[valread] = '\0';
        return 0;
    } else {
        strcpy(response_buffer, "ERROR|NO_RESPONSE");
        return -1;
    }
}


// ----------------------------------------------------
// 主邏輯函式 (修改為 Pipe 輸入)
// ----------------------------------------------------

int main() {
    char detected_plate[BUFFER_SIZE];
    char server_response[BUFFER_SIZE];
    char alarm_message[BUFFER_SIZE];
    int pipe_fd;

    // 1. 建立具名管道 (FIFO)
    // 如果檔案不存在則建立，如果已存在則忽略錯誤
    if (mkfifo(PIPE_PATH, 0666) == -1) {
        if (errno != EEXIST) {
            perror("❌ 無法建立 Pipe");
            exit(EXIT_FAILURE);
        }
    }

    printf("\n===================================\n");
    printf("🅿️ RPI #2 車位監控 Client 啟動\n");
    printf("📢 監控車位: %s\n", MONITOR_SPOT);
    printf("📡 等待 LPR 透過 Pipe (%s) 輸入車牌...\n", PIPE_PATH);
    printf("===================================\n");

    // 2. 開啟 Pipe (阻塞模式，直到有寫入端開啟才會繼續)
    // 注意：為了防止讀取一次後就收到 EOF 結束，通常會在迴圈內重新開啟，
    // 或者用 O_RDWR 開啟 (雖然不標準但可以保持 Pipe 開啟)。
    // 這裡採用標準做法：在迴圈中不斷讀取。
    
    while(1) {
        // 開啟 Pipe 讀取端 (會阻塞直到有人寫入)
        pipe_fd = open(PIPE_PATH, O_RDONLY);
        if (pipe_fd == -1) {
            perror("❌ 無法開啟 Pipe");
            sleep(1);
            continue;
        }

        // 讀取 Pipe 內容
        memset(detected_plate, 0, sizeof(detected_plate));
        int bytes_read = read(pipe_fd, detected_plate, sizeof(detected_plate) - 1);
        close(pipe_fd); // 讀取完畢後關閉，準備下一次開啟

        if (bytes_read > 0) {
            // 處理讀取到的字串 (移除換行符號)
            detected_plate[strcspn(detected_plate, "\n")] = 0;
            detected_plate[strcspn(detected_plate, "\r")] = 0; // 額外處理 Windows 換行

            // 如果讀到空字串或是 quit 指令
            if (strlen(detected_plate) == 0) continue;
            if (strcmp(detected_plate, "quit") == 0) {
                printf("👋 收到退出指令，程式結束。\n");
                break;
            }

            printf("\n🔍 [LPR 輸入] 偵測到車牌: %s\n", detected_plate);

            // --- 以下邏輯與原本相同 ---

            printf("➡️ 請求 Server 檢查車位 %s 狀態...\n", MONITOR_SPOT);
            
            // 1. 請求 Server 獲取 A1 的實際狀態
            if (request_spot_status(MONITOR_SPOT, server_response) != 0) {
                printf("❌ Server 通訊失敗或無回應。\n");
                continue;
            }

            printf("⬅️ Server 回覆: %s\n", server_response);
            
            // 2. 解析 Server 回覆: PLATE|<PlateInA1>|<Status>
            char temp_response[BUFFER_SIZE];
            strcpy(temp_response, server_response);
            char *status_tag = strtok(temp_response, "|"); // PLATE
            char *plate_in_spot = strtok(NULL, "|");       // 實際佔用車牌 (NONE 或 AAA111)
            char *spot_status = strtok(NULL, "|");         // 狀態 (EMPTY/OCCUPIED/RESERVED)
            
            if (status_tag == NULL || strcmp(status_tag, "PLATE") != 0 || plate_in_spot == NULL || spot_status == NULL) {
                printf("❌ Server 回覆格式錯誤或車位 ID 無效。\n");
                continue;
            }

            // 3. 核心監控邏輯：比對 Server 數據與 LPR 數據
            printf("[比對] LPR 偵測: %s | Server 紀錄: %s (%s)\n", detected_plate, plate_in_spot, spot_status);
            
            if (strcmp(spot_status, "OCCUPIED") == 0) {
                // A. 車位被佔用 (OCCUPIED)
                if (strcmp(plate_in_spot, detected_plate) == 0) {
                    printf("✅ 狀態正常：車牌 %s 位於預期位置 %s。\n", detected_plate, MONITOR_SPOT);
                } else {
                    snprintf(alarm_message, BUFFER_SIZE, 
                             "錯誤車輛停入！ Server 紀錄為 %s, 實際偵測為 %s", 
                             plate_in_spot, detected_plate);
                    trigger_alarm(alarm_message);
                }
            } else if (strcmp(spot_status, "EMPTY") == 0 || strcmp(spot_status, "RESERVED") == 0) {
                // B. 車位為空 (EMPTY) 或被預留 (RESERVED)
                // Monitor LPR 卻偵測到車輛，表示有未註冊車輛非法停入 
                if (strcmp(detected_plate, "NONE") != 0 && strlen(detected_plate) > 0) {
                    snprintf(alarm_message, BUFFER_SIZE, 
                             "非法停車！ 車位狀態 %s，但偵測到車輛 %s", 
                             spot_status, detected_plate);
                    trigger_alarm(alarm_message);
                } else {
                     printf("✅ 狀態正常：車位 %s 狀態為 %s，LPR 未偵測到車輛。\n", MONITOR_SPOT, spot_status);
                }
            } else {
                 printf("⚠️ 未知的車位狀態: %s\n", spot_status);
            }
        }
    }

    return 0;
}
