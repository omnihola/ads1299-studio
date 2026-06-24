
#ifndef GUARD_pathutil_hh
#define GUARD_pathutil_hh

#include <vector>
#include <string>

namespace pathutil {

void normalise(std::vector<std::string>& path);
void normalise(std::string& path);
void join(const std::vector<std::string>& p, std::string& s);
void split(const std::string& str, std::vector<std::string>& tokens);
std::string name(const std::string& p);

};

#endif
