#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <map>

#define MAX_LINE 15000

using namespace std;

struct my_cmd{
	vector<string> argv;
	int pipe_to = 0;
	string store_addr = "";
};

vector<my_cmd> C;
map< int, char** > argvs_of_cmd;
map< int, int > argcs_of_cmd;
map< int, int > pipe_num_to; // pipe_num, counter

string command_list[] = {"cat", "ls", "noop", "number", "removetag", "removetag0", "exit"};
int cmd_list_size = sizeof(command_list)/sizeof(command_list[0]);

void err_sys(const char* x) 
{ 
    perror(x); 
    exit(1); 
}

ssize_t	writen(int fid, const char *buf, size_t size)
{
	size_t	nremain;
	ssize_t	nwritten;
	const char *buf_now;

	buf_now = buf;
	nremain = size;
	while (nremain > 0) {
		if ( (nwritten = write(fid, buf_now, nremain)) <= 0) {
			// not interrupt by write()
			if (nwritten < 0 && errno == EINTR)
				nwritten = 0;
			// error
			else
				return -1;
		}

		nremain -= nwritten;
		buf_now += nwritten;
	}
	return(size);
}

void Writen(int fid, char *buf, size_t size)
{
	if (writen(fid, buf, size) != size)
		err_sys("writen error");
}

ssize_t	readn(int fd, char *vptr, size_t size)
{
	size_t nremain;
	ssize_t	nread;
	char *ptr;

	ptr = vptr;
	nremain = size;
	while (nremain > 0) {
		if ( (nread = read(fd, ptr, nremain)) < 0) {
			if (errno == EINTR)
				nread = 0;
			else
				return(-1);
		} else if (nread == 0) // EOF
			break;

		nremain -= nread;
		ptr   += nread;
	}
	return(size - nremain);
}

ssize_t Readn(int fid, char *buf, size_t size)
{
	ssize_t	n;

	if ( (n = readn(fid, buf, size)) < 0)
		err_sys("readn error");
	return(n);
}

void check_need_data(bool &need, int (&p_num)[2]){
	bool create_pipe = false;
	char buf[1024];
	for (auto s: pipe_num_to) {
		if (s.second == 0) {
			if (!create_pipe) {
				create_pipe = true;
				pipe(p_num);
			}
			while (Readn(s.first, buf, 1024) > 0) {
				Writen(p_num[1], buf, 1024);
			}
		}
		pipe_num_to.erase(s.first);
	}
	close(p_num[1]);
}

void update_pipe_num_to() {
	for (auto &s:pipe_num_to) {
		s.second--;
	}
}

void add_cmd(my_cmd &cmd, string new_cmd) {
	if (cmd.argv.size() != 0) C.push_back(cmd);
	cmd.argv.clear();
	cmd.pipe_to = 0;
	cmd.store_addr = "";
}

void process_pipe_info(my_cmd &cmd, string &s) {
	if (s == "|") {
		cmd.pipe_to = 1;
		return;
	}
	for (int i = 1; i < s.size(); i++)
		cmd.pipe_to = cmd.pipe_to *10 + (int)(s[i] - '0');
}

void sig_chld(int signo)
{
	int	pid, stat;

	while ( (pid = waitpid(-1, &stat, WNOHANG)) > 0){
        // printf("child %d terminated\n", pid);
		
		// free the memory!!
		char **cmd_argv = argvs_of_cmd[pid];
		int cmd_argc = argcs_of_cmd[pid];
		argvs_of_cmd.erase(pid);
		argcs_of_cmd.erase(pid);
		
		free(cmd_argv);
    }

	return;
}

int main(void){
	signal(SIGCHLD, sig_chld);

	int should_run = 1; 

	char buf[MAX_LINE];

	my_cmd tmp;
	string s;
	char **cmd_argv;
	while(should_run){
		cout << "\n%"; fflush(stdout);
		strcpy(buf, "");
		s = "";
		int input_length = read(0, buf ,MAX_LINE);

		// process the argument
		bool storage_flg = false;
		for(int i = 0; i < input_length; i++){
			if(buf[i] == ' ' || i == input_length-1){ 
				if (s.size() > 0) {
					// store the path
					if (storage_flg) {
						tmp.store_addr = s;
						s = "";
						storage_flg = false;
					}else{
						// get the command
						bool find = false;
						for (int j = 0; j < cmd_list_size; j++) {
							if (s == command_list[j]) {
								add_cmd(tmp, s);
								tmp.argv.push_back(s);
								find = true;
								break;
							}
						}
						
						// get pipe,storage,argument
						if (!find) {
							if (s[0] == '|') process_pipe_info(tmp, s);
							else if(s == ">") storage_flg = true;
							else tmp.argv.push_back(s);
						}
						// clear s
						s = "";
					}
				}
			}else {
				s = s + buf[i];
			}
			// store the last command
			if (i == input_length-1) add_cmd(tmp, "");
		}
		
		// execute the command
		for (int i = 0; i < C.size(); i++) {			
			int cmd_argc = C[i].argv.size();
			cmd_argv = (char**) malloc(sizeof(char*) * (cmd_argc+1));

			bool need_data, need_pipe = (C[i].pipe_to > 0);
			int pid, p_num[2], data_pipe[2];

			if (need_pipe) pipe(p_num);
			// check_need_data(need_data, data_pipe);

			// fork
			pid = fork();
			if (pid < 0) {
				cout << "failed to fork\n";
				return -1;
			}

			// parent do
			if (pid > 0) {
				// record memory allocate address
				argvs_of_cmd.insert(pair<int, char**>(pid, cmd_argv));
				argcs_of_cmd.insert(pair<int, int>(pid, cmd_argc+1));
				if (i == C.size()-1) C.clear();

				// record pipe descripter for the command behind
				if (need_pipe) {
					close(p_num[1]);
					pipe_num_to.insert(pair<int, int>(p_num[0], C[i].pipe_to));
				}

				// update pipe_num_to
				update_pipe_num_to();
				continue;
			}

			// get the arguments ready
			for (int j = 0; j < cmd_argc; j++){
				cmd_argv[j] = (char*) malloc(sizeof(char) * C[i].argv[j].size());
				strcpy(cmd_argv[j], C[i].argv[j].data());
			}
			cmd_argv[cmd_argc] = NULL;
			
			// received data from other process
			// if (need_data){
			// 	dup2(data_pipe[0], STDIN_FILENO);
			// }

			// need to pipe data to other process
			if (need_pipe) {
				close(p_num[0]);
				dup2(p_num[1], STDOUT_FILENO);
			}

			// exec!!!!
			if (execvp(cmd_argv[0], cmd_argv) < 0) {
				cout << "Bad command\n";
				free(cmd_argv);
				C.clear();
				break;
			}
		}

		// wait for all child
		while (!argvs_of_cmd.empty()) {
			sig_chld(SIGCHLD);			
		}
		
	}
	return 0;
}
