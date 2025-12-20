// reservation_client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SERVER_IP "172.21.11.211" 
#define SERVER_PORT 8888
#define BUFFER_SIZE 1024

// ----------------------------------------------------
// Socket 通訊函式
// ----------------------------------------------------

int send_command(const char *command, char *response_buffer) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    response_buffer[0] = '\0';

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("❌ Socket 建立失敗");
        return -1;
    }
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "❌ 無效的 Server IP 地址 (%s)\n", SERVER_IP);
        close(sock);
        return -1;
    }
    
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sock);
        return -1;
    }

    send(sock, command, strlen(command), 0);
    int valread = read(sock, response_buffer, BUFFER_SIZE - 1);
    
    if (valread > 0) {
        response_buffer[valread] = '\0';
    } else {
        strcpy(response_buffer, "ERROR|NO_RESPONSE");
    }
    
    close(sock);
    return 0;
}

// ----------------------------------------------------
// 選單處理與邏輯
// ----------------------------------------------------

void handle_reservation() {
    char input[BUFFER_SIZE];
    char plate[15], user_type[10], time_val[10], spot_id[5];
    char command[BUFFER_SIZE], response[BUFFER_SIZE];

    printf("\n--- 預約車位 ---\n");
    printf("請輸入 [車牌 身分 時間(HHMM) 車位ID]: ");
    if (fgets(input, sizeof(input), stdin) == NULL) return;
    if (sscanf(input, "%s %s %s %s", plate, user_type, time_val, spot_id) != 4) {
        printf("❌ 格式錯誤。\n");
        return;
    }

    snprintf(command, BUFFER_SIZE, "RESERVE_SPOT|%s|%s|%s|%s", plate, user_type, time_val, spot_id);
    if (send_command(command, response) == 0) printf("⬅️ Server 回覆: %s\n", response);
}

void handle_status_query() {
    char response[BUFFER_SIZE];
    printf("\n--- 2. 查詢目前預約/佔用狀況 ---\n");
    if (send_command("STATUS|QUERY", response) != 0) return;

    // 解析格式: STATUS|TIME:HH:MM:SS|A1,EMPTY;...
    if (strstr(response, "STATUS|") != NULL) {
        char *time_ptr = strstr(response, "TIME:");
        char *data_ptr = strchr(response + 7, '|'); 

        if (time_ptr && data_ptr) {
            char time_str[10] = {0};
            strncpy(time_str, time_ptr + 5, 8);
            printf("🕒 目前模擬時間: %s\n", time_str);
            
            char *actual_data = data_ptr + 1;
            printf("\n| 車位 | 狀態       |\n|------|------------|\n");
            char *token, *save_ptr;
            token = strtok_r(actual_data, ";", &save_ptr);
            while (token != NULL) {
                char *s_id = strtok(token, ",");
                char *s_st = strtok(NULL, ",");
                if (s_id && s_st) printf("| %-4s | %-10s |\n", s_id, s_st);
                token = strtok_r(NULL, ";", &save_ptr);
            }
        }
    }
}

void handle_release() {
    char plate[15], command[BUFFER_SIZE], response[BUFFER_SIZE];
    printf("\n--- 3. 取消預約車位 ---\n請輸入車牌: ");
    if (fgets(plate, sizeof(plate), stdin) == NULL) return;
    plate[strcspn(plate, "\n")] = 0;
    snprintf(command, BUFFER_SIZE, "RELEASE|%s", plate);
    if (send_command(command, response) == 0) printf("⬅️ Server 回覆: %s\n", response);
}

/**
 * @brief 4. 顯示目前時間
 */
void handle_display_time() {
    char response[BUFFER_SIZE];
    printf("\n--- 4. 顯示目前時間 ---\n");
    // 向 Server 請求時間，Server 會回傳 TIME|HH:MM:SS 或透過 STATUS 解析
    if (send_command("STATUS|QUERY", response) == 0) {
        char *time_ptr = strstr(response, "TIME:");
        if (time_ptr) {
            char time_val[10] = {0};
            strncpy(time_val, time_ptr + 5, 8);
            printf("🕒 目前停車場模擬時間為: %s\n", time_val);
        } else {
            printf("⚠️ Server 回傳格式不含時間資訊。\n");
        }
    }
}

void display_menu() {
    printf("\n===================================\n");
    printf("     🅿️ 停車場 Client 介面\n");
    printf("===================================\n");
    printf("1. 預約車位 (指定車位)\n");
    printf("2. 查詢目前預約/佔用狀況\n");
    printf("3. 取消預約車位\n");
    printf("4. 顯示目前時間\n");
    printf("5. 退出\n");
    printf("-----------------------------------\n");
    printf("請選擇操作 (1-5): ");
}

int main() {
    int choice;
    char dummy[BUFFER_SIZE];
    if (send_command("PING|HEARTBEAT", dummy) == -1) {
         fprintf(stderr, "❌ 無法連接 Server。\n");
         return 1;
    }

    while (1) {
        display_menu();
        if (scanf("%d", &choice) != 1) {
            while (getchar() != '\n');
            continue;
        }
        while (getchar() != '\n'); 

        switch (choice) {
            case 1: handle_reservation(); break;
            case 2: handle_status_query(); break;
            case 3: handle_release(); break;
            case 4: handle_display_time(); break;
            case 5: printf("👋 退出程式。\n"); return 0;
            default: printf("⚠️ 請輸入 1-5。\n"); break;
        }
    }
    return 0;
}