
#include <csignal> 
#include <execinfo.h>  
#include <cstdio>    
#include <cstdlib> 
#include <sys/time.h>
#include <iomanip>
#include <regex>

#include "util.h"

namespace emai {


std::string getCompileTime() {
  // 示例: "14:30:25"
  const char* compileTime = __TIME__;
  std::string timeStr(compileTime);
  
  // 移除所有冒号
  timeStr.erase(std::remove(timeStr.begin(), timeStr.end(), ':'), timeStr.end());
  
  return timeStr;
}

int monthAbbrToNumber(const char* month) {
  constexpr std::array<const char*, 12> months = {
      "Jan", "Feb", "Mar", "Apr", "May", "Jun",
      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
  };
  
  for (int i = 0; i < 12; ++i) {
      if (std::strncmp(month, months[i], 3) == 0) {
          return i + 1;
      }
  }
  return 0; // 无效月份
};

std::string getCompileDate() {
  // 示例: "Aug 19 2025"
  const char* compileDate = __DATE__;

  // 解析月份 (前3字符)
  char monthStr[4] = {0};
  std::strncpy(monthStr, compileDate, 3);
  int month = monthAbbrToNumber(monthStr);

  // 解析日期 (第5-6字符，可能包含空格)
  char dayStr[3] = {0};
  // 处理日期前可能有空格的情况
  const char* dayStart = compileDate + 4;
  if (*dayStart == ' ') dayStart++;
  std::strncpy(dayStr, dayStart, 2);
  int day = std::atoi(dayStr);

  // 解析年份 (最后4字符)
  char yearStr[5] = {0};
  std::strncpy(yearStr, compileDate + std::strlen(compileDate) - 4, 4);
  int year = std::atoi(yearStr) % 100; // 取后两位

  // 格式化为 YYMMDD
  std::ostringstream oss;
  oss << std::setfill('0') 
    << std::setw(2) << year
    << std::setw(2) << month
    << std::setw(2) << day;

  return oss.str();
}



}
