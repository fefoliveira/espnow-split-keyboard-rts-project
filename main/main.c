#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    int i = 0;
    while(1) {
        printf("Hello World, ESP1... %d!\n", i);
        // printf("Hello World, ESP2... %d!\n", i);
        i++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
