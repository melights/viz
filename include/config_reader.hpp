#pragma once

#include <string>
#include <fstream>
#include <map>
#include <vector>
#include <sstream>

namespace viz {

  class ConfigReader {

  public:
    explicit ConfigReader(const std::string &config_file){

      std::ifstream ifs(config_file);
      if (!ifs.is_open()){
        throw std::runtime_error("Error, cannot open file!");
      }

      std::string line;
      while (std::getline(ifs, line))
      {
        if (line[0] == '#' || line.length() < 1) continue;
        remove_carriage_return(line);
        std::vector<std::string> tokens = split(line, '=');
        if (tokens.size() != 2) throw std::runtime_error("Error, bad parse!");
        config_[tokens[0]] = tokens[1];
      }
    }

    void remove_carriage_return(std::string& line)
    {
      if (*line.rbegin() == '\r')
      {
        line.erase(line.length() - 1);
      }
    }

    std::string get_element(const std::string &key){

      if (config_.count(key) == 0){
        throw std::runtime_error("Couldn't find key!");
      }
      else{
        return config_[key];
      }

    }

  protected:

    std::vector<std::string> split(const std::string &s, char delim) {
      std::vector<std::string> elems;
      split(s, delim, elems);
      return elems;
    }

    std::vector<std::string> &split(const std::string &s, char delim, std::vector<std::string> &elems) {
      std::stringstream ss(s);
      std::string item;
      while (std::getline(ss, item, delim)) {
        elems.push_back(item);
      }
      return elems;
    }


    std::map<std::string, std::string> config_;


  };



}