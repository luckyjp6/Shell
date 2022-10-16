#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <map>

#include <fstream>

#define MAX_LINE 15000

using namespace std;

struct my_cmd{
	vector<string> argv;
	int pipe_to = 0;
	string store_addr = "";
	bool err = false;
	bool number_pipe = false;
};

vector<my_cmd> C;
map< size_t, char** > argvs_of_cmd;
map< size_t, int > argcs_of_cmd;
map< size_t, int > pipe_num_to; // pipe_num, counter
my_cmd tmp;

void err_sys(const char* x);
void sig_chld(int signo);

ssize_t writen(int fid, const char *buf, size_t size);
void Writen(int fid, char *buf, size_t size);
ssize_t	readn(int fid, char *buf, size_t size);
ssize_t Readn(int fid, char *buf, size_t size);

void check_need_data(bool &need, int (&p_num)[2], vector<int> &data_list);
void update_pipe_num_to();

void clear_tmp();
void process_pipe_info(string s);


int main(void){
	setenv("PATH", "bin:.", 1);
	signal(SIGCHLD, sig_chld);

	int should_run = 1; 
// ifstream in("1.txt", ios::in|ios::binary);
	string s;
	char **cmd_argv;
	while(should_run){
		printf("%% "); fflush(stdout);
		
		int input_length;

		string buf = "";
		
		if(cin.eof()) break;
		getline(cin, buf);
		// if(in.eof()) break;
		// getline(in, buf);
		input_length = buf.size();

		if (input_length < 0) {
			err_sys("failed to read\n");
			return -1;
		}
		else if (input_length <= 1) continue;
		
		if (buf[input_length-1] != '\n') {
			buf += '\n';
			input_length ++;
		}
		
		// process the argument
		s = "";
		bool storage_flg = false;
		for(int i = 0; i < input_length; i++){
			if(buf[i] == ' ' || i == input_length-1 || buf[i] == '\n' || buf[i] == '\r'){
				if (s.size() > 0) {
					// store the path
					if (storage_flg) {
						tmp.store_addr = s;
						storage_flg = false;
						C.push_back(tmp);
						clear_tmp();
						
						// clear s
						s = "";
						continue;
					}

					if (s == "|") tmp.pipe_to = 1;
					else if(s[0] == '|' || s[0] == '!') process_pipe_info(s);
					else if(s == ">") storage_flg = true;
					else tmp.argv.push_back(s);
					
					if (s[0] == '|' || s[0] == '!') {
						C.push_back(tmp);
						clear_tmp();
					}

					// clear s
					s = "";
				}
			}else s = s + buf[i];			
			
			// store the last command
			if (i == input_length-1 || buf[i] == '\n' || buf[i] == '\r') {
				if (tmp.argv.size() != 0) C.push_back(tmp);
				clear_tmp();
			};
		}

		// execute the command
		for (int i = 0; i < C.size(); i++) {
			if (C[i].argv[0] == "exit") {
				C.clear();
				should_run = false;
				break;
			}
			if (C[i].argv[0] == "setenv") {
				if (C[i].argv.size() < 3) {
					cout << "Usage: setenv [Variable] [Value].\n";
					continue;
				}
				setenv(C[i].argv[1].data(), C[i].argv[2].data(), 1);
				continue;
			}
			if (C[i].argv[0] == "printenv") {
				if (C[i].argv.size() < 2) {
					cout << "Usage: printenv [Variable].\n";
					continue;
				}
				char* env_info = getenv(C[i].argv[1].data());
				if (env_info != NULL) printf("%s\n", env_info);
				continue;
			}

			size_t cmd_argc = C[i].argv.size();
			cmd_argv = (char**) malloc(sizeof(char*) * (cmd_argc+1));

			bool need_data, need_pipe = (C[i].pipe_to > 0);
			size_t pid;
			int p_num[2], data_pipe[2];
			vector<int> data_list;

			// record pipe descripter for the command behind
			if (need_pipe) pipe(p_num);
			
			check_need_data(need_data, data_pipe, data_list);
			
			// fork
			pid = fork();
			if (pid < 0) {
				printf("failed to fork\n");
				return -1;
			}
			
			// parent do
			if (pid > 0) {
				if (need_pipe) close(p_num[1]);
				if (need_data) close(data_pipe[0]);

				// record memory allocate address
				argvs_of_cmd.insert(pair<size_t, char**>(pid, cmd_argv));
				argcs_of_cmd.insert(pair<size_t, int>(pid, cmd_argc+1));
				
				if (C[i].number_pipe || i == 0) update_pipe_num_to();
				
				if (C[i].number_pipe) pipe_num_to.insert(pair<size_t, int>(p_num[0], C[i].pipe_to-1));
				else pipe_num_to.insert(pair<size_t, int>(p_num[0], 0));

				if (need_data){//} && data_list.size() != 0) {
					pid = fork();
					if (pid < 0) {
						printf("failed to fork\n");
						return -1;
					}
					if (pid == 0) {
						for (auto id: data_list) {
							char read_data[1024];
							int read_length;
							while ((read_length = Readn(id, read_data, 1024)) > 0) {
								Writen(data_pipe[1], read_data, read_length);
							}
							close(id);
						}
						close(data_pipe[1]);
						return 0;
					}else{
						close(data_pipe[1]);
						

						// signal handler will wait for the child
						argvs_of_cmd.insert(pair<int, char**>(pid, NULL));
						argcs_of_cmd.insert(pair<int, int>(pid, 0));
					}
				}				
				continue;
			}
			
			// child do
			for(auto id: data_list) close(id);
			data_list.clear();
			// received data from other process
			if (need_data){
				//if (data_list.size() != 0) {
					close(data_pipe[1]);
					
				//}
				dup2(data_pipe[0], STDIN_FILENO);
			}

			// pipe stderr
			if (C[i].err) dup2(p_num[1], STDERR_FILENO);
			
			// need to pipe data to other process
			if (need_pipe) {
				close(p_num[0]);
				dup2(p_num[1], STDOUT_FILENO);
			}

			// store
			if (C[i].store_addr.size() != 0) {
				size_t file_id = open(C[i].store_addr.data(), O_WRONLY|O_CREAT|O_TRUNC, 00777);
				ftruncate(file_id, 0);
				lseek(file_id, 0, SEEK_SET);
				if (file_id < 0) {
					printf("Failed to open file %s\n", C[i].store_addr.data());
					free(cmd_argv);
					continue;
				}
				dup2(file_id, STDOUT_FILENO);
			}

			// get the arguments ready
			for (int j = 0; j < cmd_argc; j++){
				cmd_argv[j] = (char*) malloc(sizeof(char) * C[i].argv[j].size());
				strcpy(cmd_argv[j], C[i].argv[j].data());
			}
			cmd_argv[cmd_argc] = new char;
			cmd_argv[cmd_argc] = NULL;

			// exec!!!!
			if (execvp(cmd_argv[0], cmd_argv) < 0) {
				printf("Unknown command: [%s].\n", cmd_argv[0]);
				return -1;
			}
		}
		
		while (!argvs_of_cmd.empty()) {
			sig_chld(SIGCHLD);			
		}
		C.clear();
	}
	
	// wait for all child
	while (!argvs_of_cmd.empty()) {
		sig_chld(SIGCHLD);			
	}
	return 0;
}

void check_need_data(bool &need, int (&p_num)[2], vector<int> &data_list) {
	for (auto s: pipe_num_to) { 
		// counter == 0 => pipe the data to the command execute next
		if (s.second == 0) data_list.push_back(s.first);		
	}

	need = data_list.size() > 0;
	if (!need) return;

	pipe(p_num);
	for (auto s: data_list) {
		pipe_num_to.erase(s);
	}
	
	// if(data_list.size() == 1) {
	// 	p_num[0] = data_list[0];
	// 	data_list.clear();
	// 	return;
	// }

	// parent will tackle the data from multiple pipe
	
}

void update_pipe_num_to() {
	vector<int> wait_to_erase;
	for (auto &s: pipe_num_to) {
		s.second--;
		if (s.second < 0) wait_to_erase.push_back(s.first);
	}
	for (auto s: wait_to_erase) pipe_num_to.erase(s);
}

void clear_tmp() {
	tmp.argv.clear();
	tmp.pipe_to = 0;
	tmp.store_addr = "";
	tmp.err = false;
	tmp.number_pipe = false;
}

void process_pipe_info(string s) {
	if (s[0] == '!') tmp.err = true;
	tmp.number_pipe = true;
	for (int i = 1; i < s.size(); i++)
		tmp.pipe_to = tmp.pipe_to *10 + (int)(s[i] - '0');
}

void sig_chld(int signo)
{
	int	pid, stat;

	while ( (pid = waitpid(-1, &stat, WNOHANG)) > 0){
        // printf("child %d terminated\n", pid);
		
		// free the memory!!
		char **cmd_argv = argvs_of_cmd[pid];
		argvs_of_cmd.erase(pid);
		argcs_of_cmd.erase(pid);
		
		if (cmd_argv != NULL) free(cmd_argv);
    }

	return;
}

void err_sys(const char* x) { 
    perror(x); 
    exit(1); 
}

ssize_t	writen(int fid, const char *buf, size_t size) {
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

void Writen(int fid, char *buf, size_t size) {
	if (writen(fid, buf, size) != size)
		err_sys("writen error");
}

ssize_t	readn(int fid, char *buf, size_t size) {
	size_t nremain;
	ssize_t	nread;
	char *buf_now;

	buf_now = buf;
	nremain = size;
	while (nremain > 0) {
		if ( (nread = read(fid, buf_now, nremain)) < 0) {
			if (errno == EINTR)
				nread = 0;
			else
				return -1;
		} else if (nread == 0) { // EOF
			break;
		}
		nremain -= nread;
		buf_now += nread;
	}
	return(size - nremain);
}

ssize_t Readn(int fid, char *buf, size_t size) {
	size_t	n;

	if ( (n = readn(fid, buf, size)) < 0)
		err_sys("readn error\n");
	return(n);
}
