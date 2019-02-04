#include "ir.h"
#include "mqtt.h"

#include <Arduino.h>

extern "C" {
    #include <i2s_reg.h>
    #include <gpio.h>
}

#define TIMER_INTERVAL_US   (62)
#define IR_OUTPUT_PIN       (14)

#define MAX_DURATIONS       (1024)

namespace
{
    uint16_t currentDuration = 0;
    uint16_t numParsedDurations = 0;
    uint16_t parsedDurations[MAX_DURATIONS];

    void startCarrier()
    {
        WRITE_PERI_REG(PERIPHS_IO_MUX_MTMS_U,
            READ_PERI_REG(PERIPHS_IO_MUX_MTMS_U)
            & 0xfffffe0f
            | (0x1 << 4)
        );
    
        WRITE_PERI_REG(I2SCONF,
            READ_PERI_REG(I2SCONF)
            & 0xf0000fff | (                                                 // Clear I2SRXFIFO, BCK_DIV and CLKM_DIV sections
                ((TIMER_INTERVAL_US & I2S_BCK_DIV_NUM) << I2S_BCK_DIV_NUM_S) // Set the clock frequency divider
                | ((2 & I2S_CLKM_DIV_NUM) << I2S_CLKM_DIV_NUM_S)             // Set the clock prescaler
                | ((1 & I2S_BITS_MOD) << I2S_BITS_MOD_S)                     // ?
            )
        );

        WRITE_PERI_REG(I2SCONF,
            READ_PERI_REG(I2SCONF) | I2S_I2S_RX_START                       // Set the I2S_I2S_RX_START bit
        );
    }

    void stopCarrier()
    {
        WRITE_PERI_REG(I2SCONF,
            READ_PERI_REG(I2SCONF) & 0xfffffdff                             // Clear I2S_I2S_RX_START bit
        );

        PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U, FUNC_GPIO14);                // Set the MTMS pin function to standard GPIO
        GPIO_OUTPUT_SET(GPIO_ID_PIN(IR_OUTPUT_PIN), 0);                     // Clear the output
    }
}

namespace ir
{
    void setupSend()
    {
        stopCarrier();
        clearCommandBuffer();
    }

    void clearCommandBuffer()
    {
        currentDuration = 0;
        numParsedDurations = 0;
    }

    void parseDurations(const char *message, size_t len, bool messageComplete)
    {
        // Parse the supplied message and extract the carrier on/off durations.
        // The message should be formatted as a sequence of space-separated integers.
        // The first represents the ON time, the second the OFF time, and so on repeatedly.
        uint16_t index = 0;
        while (index < len && numParsedDurations < MAX_DURATIONS)
        {
            // Iterate over the next sequence of digits and update currentDuration
            while (index < len && isdigit(message[index]))
            {
                currentDuration = currentDuration * 10 + (message[index] - 48);
                ++index;
            }
            
            // If we found a space, or EOF in the final part of the message, then store the current duration.
            // Otherwise we might have a partially-parsed duration in a multipart message and can't store it yet.
            if (isspace(message[index]) || (index == len && messageComplete))
            {
               parsedDurations[numParsedDurations++] = currentDuration;
               currentDuration = 0;
            }

            // Skip the next chars until we find another digit or EOF (theoretically after exactly one space)
            while (index < len && isdigit(message[index]) == false)
                ++index;
        }
    }

    void transmit()
    {
        stopRecording();

        for (int index = 0; index < numParsedDurations; index += 2)
        {
            startCarrier();
            delayMicroseconds(parsedDurations[index]);
            
            stopCarrier();
            if (index < numParsedDurations)
                delayMicroseconds(parsedDurations[index + 1]);
        }

        clearCommandBuffer();
        startRecording();

        mqtt::publishLog("Transmitted IR command");
    }
}
