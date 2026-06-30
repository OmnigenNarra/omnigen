#pragma once
#include "dirent.h"
#include <string>

class FileCrawler
{
public:
    template<typename Callable>
    static bool Run(Callable C, const std::string& current_dir = ".")
    {
        DIR* dir;
        dirent* ent;

        std::string name;

        if ((dir = opendir(current_dir.c_str())) == NULL)
            return false;

        while ((ent = readdir(dir)) != NULL)
        {
            if (ent->d_namlen < 3)
                continue;

            name = current_dir + "/" + ent->d_name;

            if (ent->d_type == DT_DIR)
                Run(C, name);
            else
                C(name);
        }

        closedir(dir);
        return true;
    }
};