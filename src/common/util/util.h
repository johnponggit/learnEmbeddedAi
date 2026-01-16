
#pragma once

#include <string>
#include <iostream>
#include <unistd.h> 
#include <thread>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/file.h>
#include <malloc.h>
#include <memory> 
#include <utility>

#include "LogMacros.h"


namespace emai {
  
    std::string getCompileTime();
    std::string getCompileDate();

 
}
