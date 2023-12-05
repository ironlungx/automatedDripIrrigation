#pragma once

#include <FS.h>
#include <SPIFFS.h>


class SpiffsHelper
{
public:
    static String readFile(const char *filePath)
    {
        File file = SPIFFS.open(filePath, "r");
        if (!file) { return String(); }

        String fileContent = file.readString();
        file.close();

        return fileContent;
    }

    static void writeFile(const char* filePath, String contents)
    {
        File file = SPIFFS.open(filePath, "w");
        if (file)
        {
            file.print(contents);
            file.close();
        }
    }
};
