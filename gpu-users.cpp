// g++ gpu-users.cpp --std=c++11 -O3 -o gpu-users

// watch -n 300 ./pl_gpu_store_log -f mylog.txt
// => Eso parece funcionar, pero los usuarios nuevos no aparecen entre medio...

// nvidia-smi --query-gpu=timestamp,utilization.memory --format=csv

#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <array>
#include <vector>
#include <unistd.h>
#include <sstream>
#include <map>
#include <chrono>
#include <ctime>
#include <fstream>

std::string exec(const char* cmd)
{
  std::array<char, 128> buffer;
  std::string result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
  if (!pipe)
  {
    throw std::runtime_error("popen() failed!");
  }
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
  {
    result += buffer.data();
  }
  return result;
}

static bool contained(char c, const std::string &s)
{
  for (char x : s)
  {
    if (x == c)
      return true;
  }
  return false;
}

static std::string strip(std::string s, std::string delims)
{
  if (s == "")
    return "";
  std::stringstream ss;
  int i1=0, i2=s.size()-1;
  while ( i1 < i2 && contained(s[i1], delims) )
    i1++;
  if (i1 == i2)
    return "";
  while(contained(s[i2], delims))
    i2--;
  for (size_t i=i1; i<=i2; i++)
    ss << s[i];
  return ss.str();

}

static std::vector<std::string> split(std::string s, std::string delims)
{
  std::vector<std::string> sarr;
  s = strip(s, delims);
  if (s == "")
  	return sarr;
  bool inside = true;
  int i = 0;
  while (i < s.size())
  {
    std::stringstream ss;
    while (i < s.size() && inside && !contained(s[i], delims))
    {
      ss << s[i];
      i++;
    }
    sarr.push_back(ss.str());
    inside = false;
    while (i < s.size() && !inside && contained(s[i], delims))
    {
      i++;
    }
    inside = true;
  }
  return sarr;
}

class ContainerInfoRow
{
public:
  std::vector<std::string> users_;
  std::vector<int> users_pids_;
  std::vector<std::vector<std::string>> possible_imgs_;
  std::vector<std::string> containers_;
  std::vector<std::vector<int>> pids_;
  std::string ps_run_output_;
  std::string docker_ps_output_;
  std::vector<std::string> docker_top_output_;
  bool debug_;
 
  void getPsRunInfo()
  {
    ps_run_output_ = exec("ps aux | grep docker | grep -v root");
  }

  void parsePsRunInfo()
  {
    std::stringstream ss(ps_run_output_);
    possible_imgs_.resize(0);
    users_.resize(0);
    std::string line;
    while( std::getline(ss, line) )
    {
      std::vector<std::string> tokens=split(line, " ");
      std::vector<std::string> imgs;
      if (tokens.size() < 11)
        continue;
      if (tokens[10] == "grep")
        continue;
      for (int i=11; i<tokens.size(); i++)
      {
        std::string &s = tokens[i];
        if (s[0] == '-')
          continue;
        imgs.push_back(s);
      }
      possible_imgs_.push_back(imgs);
      users_.push_back(tokens[0]);
      users_pids_.push_back(std::stoi(tokens[1]));
    }
  }

  void improveUserInfo()
  {
    for (int i=0; i<users_.size(); i++)
    {
      std::stringstream ss;
      ss << "ps axo user:40,pid | grep " << users_pids_[i];
      std::string ps_axo_output = exec(ss.str().c_str());
      std::vector<std::string> tokens = split(ps_axo_output, " ");
      if (tokens.size() > 0)
        users_[i] = tokens[0];
    }
  }

  void getDockerPsInfo()
  {
    docker_ps_output_ = exec("docker ps");
  }

  void parseDockerPsInfo()
  {
    std::stringstream ss(docker_ps_output_);
    containers_.resize(0);
    for (int i=0; i<possible_imgs_.size(); i++)
    {
      containers_.push_back("(nada)");
    }

    std::string line;
    while( std::getline(ss, line) )
    {
      std::vector<std::string> tokens=split(line, " ");
      if (tokens.size() < 1)
        continue;
      bool found = false;
      for (const std::string &t : tokens)
      {
        for (int i=0; i<possible_imgs_.size(); i++)
        {
          for (const std::string &s : possible_imgs_[i])
          {
            if (t == s)
            {
              containers_[i] = tokens[0];
              found = true;
              break;
            }
          }
          if (found)
            break;
        }
        if (found)
          break;
      }
    }
  }

  void getDockerTopInfo()
  {
    docker_top_output_.resize(0);
    for (const std::string &s : containers_)
    {
      if (s == "(nada)")
        docker_top_output_.push_back("(none)");
      else
      {
        std::stringstream ss;
        ss << "docker container top " << s;
        docker_top_output_.push_back( exec( ss.str().c_str() ) );    
      }
    }
  }
  void parseDockerTopInfo()
  {
    pids_.resize(0);
    for (int i=0; i<docker_top_output_.size(); i++)
    {
      std::stringstream ss(docker_top_output_[i]);
      std::string line;
      std::vector<int> sarr;
      while( std::getline(ss, line) )
      {
        std::vector<std::string> tokens=split(line, " ");
        if (tokens.size() < 2)
          continue;
        if (tokens[0] == "UID")
          continue;
        sarr.push_back(std::stoi(tokens[1]));
      }
      pids_.push_back(sarr);
    }
  }
  void printSummary()
  {
    for (int i=0; i<users_.size(); i++)
    {
      std::cout << "User: " << users_[i] << std::endl;
      for (int p : pids_[i])
        std::cout << "  " << p;
     std::cout << std::endl;
    }
  }

};


class GpuInfoRow
{
public:
  std::vector<int> gpu_use_id_;
  std::vector<double> gpu_use_;
  std::vector<int> gpu_pids_id_;
  std::vector<int> gpu_pids_;
  std::vector<std::string> users_home_;
  std::vector<std::string> users_;
  std::string nvidia_smi_output_;
  std::string ps_output_;
  bool debug_;
 
  enum Section {date, header_use, lin1, lin2, lin3, header_processes, processes, end } section_;

  void getSmiInfo()
  {
    nvidia_smi_output_ = exec("nvidia-smi");  // "nvidia-smi --query-gpu=utilization.gpu --format=csv "
  }
 
  void parseSmiInfo()
  {
    std::stringstream ss(nvidia_smi_output_);
    std::string line;
    std::vector<std::string> tokens;
   
    gpu_use_id_.resize(0);
    gpu_use_.resize(0);
    gpu_pids_id_.resize(0);
    gpu_pids_.resize(0);
   
    section_ = date;
    
    while( std::getline(ss, line) )
    {
      tokens = split(line, " ");
      switch(section_)
      {
        case date:
        {
          if (debug_)
          {
            std::cout << std::endl;
            std::cout << "DAT " << line << std::endl;
          }
          section_ = header_use;
          break;
        }
        case header_use:
        {
          if (debug_) {std::cout << "HEA " << line << std::endl;}
          if (line.find("|==") != std::string::npos)
          {
            section_ = lin1;
          }
          break;
        }

        case lin1:
        {
          if (debug_) {std::cout << "LI1 " << line << std::endl;}
          if (line.size() > 3 && int(line[0]) == 32)
          {
            section_ = header_processes;
          }
          else
          {
            if (tokens.size() > 1)
            {
              gpu_use_id_.push_back(std::stoi(tokens[1]));
            }
            else
            {
              if (debug_) {std::cout << "ERROR!!!" << std::endl;}
            }
            section_ = lin2;
          }
          break;
        }
        case lin2:
        {
          if (debug_) {std::cout << "LI2 " << line << std::endl;}
          for (std::string token : tokens)
          {
            if (token.find("%") != std::string::npos)
            {
              gpu_use_.push_back(std::stoi(token));
            }
          }
          section_ = lin3;
          break;
        }
        case lin3:
        {
          if (debug_) {std::cout << "LI3 " << line << std::endl;}
          if (line.find("+---") != std::string::npos)
          {
            section_ = lin1;
          }
          break;
        }
        
        case header_processes:
        {
          if (debug_) {std::cout << "HE2 " << line << std::endl;}
          if (line.find("|==") != std::string::npos)
          {
            section_ = processes;
          }
          break;
        }

        case processes:
        {
          if (debug_) {std::cout << "PRO " << line << std::endl;}
          if (line.find("No running processes found") != std::string::npos || line.find("+---") != std::string::npos)
          {
            section_ = end;
          }
          else
          {
            if (tokens.size() > 2)
            {
              gpu_pids_id_.push_back(std::stoi(tokens[1]));
              gpu_pids_.push_back(std::stoi(tokens[2]));
            }
            else
            {
              if (debug_) {std::cout << "ERROR!!!" << std::endl;}
            }
          }
          break;
        }
        case end:
        {
          if (debug_) {std::cout << "END " << line << std::endl;}
          break;  
        }
      }
    }
  }
 
  /*
  void getPsInfo()
  {
    std::stringstream ss;
    for (int id : gpu_pids_)
    {
      std::string s = "ps aux | grep " + std::to_string(id);
      std::string out = exec(s.c_str());
      ss << out;
    }
    ps_output_ = ss.str();
    if (debug_) {std::cout << ps_output_ << std::endl;}
  }
 
  void parsePsInfo()
  {
    std::stringstream ss(ps_output_);
    users_home_.resize(0);
    for (int id : gpu_use_id_)
    {
      users_home_.push_back("(null)");
    }
    std::string line;
    while( std::getline(ss, line) )
    {
      std::vector<std::string> tokens=split(line, " ");
      if (tokens.size() < 7)
        continue;
      int pid = std::stoi(tokens[1]);
      if (debug_) {std::cout << pid << std::endl;}
      for (int i=0; i<gpu_pids_.size(); i++)
      {
        if (debug_) {std::cout << " " << gpu_pids_[i] << std::endl;}
        if (pid == gpu_pids_[i])
        {
          for (std::string &t : tokens)
          {
            if (t.find("home") != std::string::npos)
            {
              if (debug_) {std::cout << t << std::endl;}
              users_home_[gpu_pids_id_[i]] = t;
            }
          }
        }
      }
    }
  }
 */

  void resolveUsers(ContainerInfoRow &cont)
  {
    users_.resize(0);
    for (int id : gpu_use_id_)
    {
      users_.push_back("(none)");
    }

    for (int i=0; i<cont.pids_.size(); i++)
    {
      for (int p : cont.pids_[i])
      {
        for (int j=0; j<gpu_pids_.size(); j++)
        if (gpu_pids_[j] == p)
          users_[ gpu_pids_id_[j] ] = cont.users_[i];
      }
    }
  }
  
  void printSummary()
  {
    std::cout << "Number of gpus: " << gpu_use_.size() << std::endl;
    std::cout << "Gpu load:" << std::endl;
    for (int i = 0; i<gpu_use_.size(); i++)
    {
      std::cout << gpu_use_id_[i] << ":" << gpu_use_[i] << " ";
    }
    std::cout << std::endl;
    std::cout << "PIDS of processes on gpu (" << gpu_pids_.size() << " processes): " << std::endl;
    for (int i=0; i<gpu_pids_.size(); i++)
    {
      std::cout << gpu_pids_id_[i] << ":" << gpu_pids_[i] << " ";
    }
    std::cout << std::endl;
    if (users_home_.size() > 0)
    {
      std::cout << "Users (from folder):" << std::endl;
      for (int i=0; i<gpu_use_.size(); i++)
      {
        std::cout << gpu_use_id_[i] << ":" << users_home_[i] << " ";
      }
      std::cout << std::endl;
    }
    std::cout << "Users per gpu (docker top):" << std::endl;
    for (int i=0; i<gpu_use_.size(); i++)
    {
      std::cout << gpu_use_id_[i] << ":" << users_[i] << " ";
    }
    std::cout << std::endl;
  }

  void printUsers(std::string outfile = "")
  {
    std::vector<std::string> strs;
    std::stringstream outtext;
    for (int i=0; i<gpu_use_.size(); i++)
    {
      strs.push_back("(unused)");
    }
    for (int i=0; i<gpu_pids_id_.size(); i++)
    {
      strs[gpu_pids_id_[i]] = "(n/a)";
    }
    for (int i=0; i<gpu_use_.size(); i++)
    {
      if (users_[i] != "(none)")
      {
         strs[i] = users_[i];
      }
    }

    outtext << "  ";
    for (int i=0; i<strs.size(); i++)
    {
      int slen = strs[i].size() - 1;
      for (int j=0; j<slen/2; j++)
      {
        outtext << " ";
      }
      outtext << "#" << i;
      for (int j=0; j<(slen+1)/2; j++)
      {
        outtext << " ";
      }
    }
    outtext << "\n";
 
    outtext << gpu_use_.size() << " "; // number of gpus, would be 8

    for (std::string &s: strs)
    {
      outtext << s << " ";
    }
    auto curr_time = std::chrono::system_clock::now();
    std::time_t end_time = std::chrono::system_clock::to_time_t(curr_time);
    outtext << std::ctime(&end_time);

    if (outfile == "")
    {
      std::cout << outtext.str();
    }
    else
    {
      std::ofstream f;
      f.open(outfile, std::ofstream::out | std::ofstream::app);
      if (!f.good())
      {
        std::cout << "File " << outfile << " could not be open" << std::endl;
        return;
      }
      f << outtext.str();
    }
  }
};



int main(int argc, char *argv[])
{
  bool debug = false;
  std::string outfile = "";

  for (int i=1; i<argc; i++)
  {
    if (std::string(argv[i]) == "--debug")
      debug = true;
  }

  for (int i=1; i<argc-1; i++)
  {
    if (std::string(argv[i]) == "-f")
      outfile = argv[i+1];
  }

  GpuInfoRow row;
  row.debug_ = debug;

  row.getSmiInfo();
  row.parseSmiInfo();
  //row.getPsInfo();
  //row.parsePsInfo();
 
  ContainerInfoRow crow;
  crow.debug_ = debug;

  crow.getPsRunInfo();
  crow.parsePsRunInfo();
  crow.improveUserInfo();
  crow.getDockerPsInfo();
  crow.parseDockerPsInfo();
  crow.getDockerTopInfo();
  crow.parseDockerTopInfo();

  if (debug) {crow.printSummary();}

  row.resolveUsers(crow);

  if(debug) {row.printSummary();}

  row.printUsers(outfile);

  return 0;
}