#include "SD_MMC.h"

/* 
 * Notes
 * Connect the SD card to the following pins:
 *
 * SD Card | ESP32
 *    D2       12
 *    D3       13
 *    CMD      15
 *    VSS      GND
 *    VDD      3.3V
 *    CLK      14
 *    VSS      GND
 *    D0       2  (add 1K pull up after flashing)
 *    D1       4
 */

#define MB 1024 * 1024

bool InitializeSD()
{
    if (!SD_MMC.begin())
    {
        Serial.println("Card Mount Failed.");
        return false;
    }

    sdcard_type_t cardType = SD_MMC.cardType();

    if (cardType == CARD_NONE)
    {
        Serial.println("No SD card attached.");
        return false;
    }

    Serial.print("SD Card Type: ");

    if (cardType == CARD_MMC)
        Serial.println("MMC.");
    else if (cardType == CARD_SD)
        Serial.println("SDSC.");
    else if (cardType == CARD_SDHC)
        Serial.println("SDHC.");
    else
        Serial.println("UNKNOWN.");

    Serial.printf("SD Card Size: %lluMB.\n", SD_MMC.cardSize() / MB);
    Serial.printf("\tTotal space: %lluMB\n", SD_MMC.totalBytes() / MB);
    Serial.printf("\tUsed space: %lluMB\n", SD_MMC.usedBytes() / MB);

    return true;
}

long sdFreeSpace()
{
    return SD_MMC.totalBytes() - SD_MMC.usedBytes();
}

String readFile(char *path)
{
    Serial.print("Reading file: ");
    Serial.println(path);

    File file = SD_MMC.open(path);
    if (!file)
    {
        Serial.println("Failed to open file for reading.");
        return "";
    }

    Serial.print("Read from file: ");
    String text;

    while (file.available())
    {
        text = file.readString();
    }

    file.close();

    return text;
}

void writeFile(const char *path, const char *text)
{
    Serial.print("Writing file: ");
    Serial.println(path);

    File file = SD_MMC.open(path, FILE_WRITE);
    
    if (!file)
    {
        Serial.println("Failed to open file for writing.");
        return;
    }
    
    if (file.print(text))
        Serial.println("File written.");
    else
        Serial.println("Write failed.");
    
    file.close();
}