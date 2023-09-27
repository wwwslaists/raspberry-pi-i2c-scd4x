#include <stdio.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <jansson.h> // Include the JSON library

#include "scd4x_i2c.h"
#include "sensirion_common.h"
#include "sensirion_i2c_hal.h"

#include <wiringPi.h>
#include <wiringPiSPI.h>
#include <stdint.h>
#include <sys/time.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

#include <linux/spi/spidev.h>

#include <netinet/in.h>
#include <sys/types.h>
#include <stdlib.h>

#include <pthread.h>

#define SPI_SPEED 500000
#define SPI_CHANNEL 0

// Define the Flask app and global variables
static struct {
    uint16_t co2;
    float temperature;
    float humidity;
    char timestamp[20];
} sensor_data;

// Flask app instance
#include <flask/flask.h>
flask_app_t *app;

// Function to get sensor data
static json_t *get_sensor_data() {
    json_t *data = json_object();
    json_object_set(data, "timestamp", json_string(sensor_data.timestamp));
    json_object_set(data, "co2", json_integer(sensor_data.co2));
    json_object_set(data, "temperature", json_real(sensor_data.temperature));
    json_object_set(data, "humidity", json_real(sensor_data.humidity));
    return data;
}

// Define a route to get sensor data
static flask_response_t *get_data_route(flask_request_t *request) {
    json_t *data = get_sensor_data();
    return flask_json_response(data, 200);
}

int main(void) {
    int16_t error = 0;

    // Initialize wiringPi and SPI
    if (wiringPiSPISetup(SPI_CHANNEL, SPI_SPEED) < 0) {
        perror("wiringPiSPISetup");
        return 1;
    }

    // Initialize Flask app
    app = flask_app_create();

    // Clean up potential SCD40 states
    scd4x_wake_up();
    scd4x_stop_periodic_measurement();
    scd4x_reinit();

    uint16_t serial_0;
    uint16_t serial_1;
    uint16_t serial_2;
    error = scd4x_get_serial_number(&serial_0, &serial_1, &serial_2);
    if (error) {
        printf("Error executing scd4x_get_serial_number(): %i\n", error);
    } else {
        printf("serial: 0x%04x%04x%04x\n", serial_0, serial_1, serial_2);
    }

    // Start Measurement
    error = scd4x_start_periodic_measurement();
    if (error) {
        printf("Error executing scd4x_start_periodic_measurement(): %i\n",
               error);
    }

    printf("Waiting for first measurement... (5 sec)\n");

    for (;;) {
        // Get the current timestamp
        time_t current_time;
        time(&current_time);

        // Convert the timestamp to a string
        strftime(sensor_data.timestamp, sizeof(sensor_data.timestamp), "%Y-%m-%d %H:%M:%S", localtime(&current_time));

        // Read Measurement if data is available
        bool data_ready_flag = false;
        sensirion_i2c_hal_sleep_usec(100000);
        error = scd4x_get_data_ready_flag(&data_ready_flag);
        if (error) {
            printf("Error executing scd4x_get_data_ready_flag(): %i\n", error);
            continue;
        }
        if (!data_ready_flag) {
            continue;
        }
        error = scd4x_read_measurement(&sensor_data.co2, &sensor_data.temperature, &sensor_data.humidity);
        if (error) {
            printf("Error executing scd4x_read_measurement(): %i\n", error);
        } else if (sensor_data.co2 == 0) {
            printf("Invalid sample detected, skipping.\n");
        } else {
            printf("Timestamp: %s\n", sensor_data.timestamp);
            printf("CO2: %u ppm\n", sensor_data.co2);
            printf("Temperature: %.2f °C\n", sensor_data.temperature);
            printf("Humidity: %.2f RH\n", sensor_data.humidity);
        }

        // Sleep for some time before the next measurement
        sleep(5);

        // Start Flask app (REST API) in a separate thread
        pthread_t flask_thread;
        pthread_create(&flask_thread, NULL, flask_run, app);
    }

    return 0;
}
