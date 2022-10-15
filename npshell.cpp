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
	bool err = false;
	bool number_pipe = false;
	bool next_line = false;
};

vector<my_cmd> C;
map< int, char** > argvs_of_cmd;
map< int, int > argcs_of_cmd;
map< int, int > pipe_num_to; // pipe_num, counter
my_cmd tmp;

string command_list[] = {"cat", "ls", "noop", "number", "removetag", "removetag0", "exit", "printenv", "setenv"};
int cmd_list_size = sizeof(command_list)/sizeof(command_list[0]);

void err_sys(const char* x);
void sig_chld(int signo);

ssize_t writen(int fid, const char *buf, size_t size);
void Writen(int fid, char *buf, size_t size);
ssize_t	readn(int fid, char *buf, size_t size);
ssize_t Readn(int fid, char *buf, size_t size);

void check_need_data(bool &need, int (&p_num)[2]);
void update_pipe_num_to();

void clear_tmp();
void process_pipe_info(string s);

int main(void){
	signal(SIGCHLD, sig_chld);

	int should_run = 1; 

	// char buf[MAX_LINE];
// int file_no = open("1.txt", O_RDONLY);
	// my_cmd tmp;
	string s;
	char **cmd_argv;
	while(should_run){
		printf("%% "); fflush(stdout);
		// strcpy(buf, "");
		
		int input_length;

		// input_length = read(0, buf ,MAX_LINE);
		string buf = "";
		getline(cin, buf);
		input_length = buf.size();

		if (input_length < 0) {
			err_sys("failed to read\n");
			return -1;
		}else if (input_length == 0){
		 	cout << "nothing\n";
			// goto again;
		 	return -1;
		}
		
		// if (buf[input_length-1] != '\n') buf[input_length++] = '\n';
		if (buf[input_length-1] != '\n') {
			buf += '\n';
			input_length ++;
		}
		update_pipe_num_to();
		
		s = "";
		// process the argument
		bool storage_flg = false;
		for(int i = 0; i < input_length; i++){
			if(buf[i] == ' ' || i == input_length-1 || buf[i] == '\n' || buf[i] == '\r'){
				// printf("args: %s\n", s.data());
				if (buf[i] == '\n' || buf[i] == '\r') tmp.next_line = true; 
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
								if (tmp.argv.size() != 0) C.push_back(tmp);
								clear_tmp();
								tmp.argv.push_back(s);
								// for (auto t:tmp.argv) cout << t << " " << endl;
								find = true;
								break;
							}
						}
										
						// get pipe,storage,argument
						if (!find) {
							if (s == "|") tmp.pipe_to = 1;
							else if(s[0] == '|' || s[0] == '!') process_pipe_info(s);
							else if(s == ">") storage_flg = true;
							else tmp.argv.push_back(s);
						}
						// clear s
						s = "";
					}
				}
			}else s = s + buf[i];
			
			// store the last command
			if (i == input_length-1 || buf[i] == '\n' || buf[i] == '\r') {
				if (tmp.argv.size() != 0) C.push_back(tmp);
				clear_tmp();
			};
		}
// for (auto s:C){
// 	for (int i = 0; i < s.argv.size(); i++) cout << s.argv[i] << " ";
// 	cout << endl; 
// }
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
					// Writen(1, err_msg, sizeof(err_msg));
					continue;
				}
				setenv(C[i].argv[1].data(), C[i].argv[2].data(), 1);
				// C.clear();
				continue;
			}
			if (C[i].argv[0] == "printenv") {
				if (C[i].argv.size() < 2) {
					cout << "Usage: printenv [Variable].\n";
					// Writen(1, err_msg, sizeof(err_msg));
					continue;
				}
				char* env_info = getenv(C[i].argv[1].data());
				if (env_info != NULL) printf("%s\n", env_info);
				// C.clear();
				continue;
			}

			int cmd_argc = C[i].argv.size();
			cmd_argv = (char**) malloc(sizeof(char*) * (cmd_argc+1));

			bool need_data, need_pipe = (C[i].pipe_to > 0);
			int pid, p_num[2], data_pipe[2];

			if (need_pipe) pipe(p_num);
			
			// fork
			pid = fork();
			if (pid < 0) {
				printf("failed to fork\n");
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
					if (C[i].number_pipe) {
						pipe_num_to.insert(pair<int, int>(p_num[0], C[i].pipe_to));
						// number pipe -> new line
						update_pipe_num_to();
					}
					else if (C[i].next_line)update_pipe_num_to();
					else pipe_num_to.insert(pair<int, int>(p_num[0], 0));
				}

				// update pipe_num_to
				// update_pipe_num_to();
				continue;
			}

			// get the arguments ready
			for (int j = 0; j < cmd_argc; j++){
				cmd_argv[j] = (char*) malloc(sizeof(char) * C[i].argv[j].size());
				strcpy(cmd_argv[j], C[i].argv[j].data());
			}
			cmd_argv[cmd_argc] = NULL;
			// received data from other process
			check_need_data(need_data, data_pipe);
			if (need_data){
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
				int file_id = open(C[i].store_addr.data(), O_WRONLY|O_CREAT, 00777);
				if (file_id < 0) {
					printf("Failed to open file %s\n", C[i].store_addr.data());
					free(cmd_argv);
					C.clear();
					break;
				}
				dup2(file_id, STDOUT_FILENO);
			}

			// exec!!!!
			if (execvp(cmd_argv[0], cmd_argv) < 0) {
				printf("Unknown command: [%s].\n", cmd_argv[0]);
				return 0;
			}
		}

		// wait for all child
		while (!argvs_of_cmd.empty()) {
			sig_chld(SIGCHLD);			
		}
		C.clear();
	}
	return 0;
}

void check_need_data(bool &need, int (&p_num)[2]) {
	bool create_pipe = false;
	char buf[1024];
	vector<int> wait_to_erase;
	for (auto s: pipe_num_to) {
		// counter == 0 => pipe the data to the command execute next
		if (s.second == 0) {
			need = true;
			// check if this command is terminated
			while (argvs_of_cmd.find(s.first) != argvs_of_cmd.end()) {
				// if not, wait until it's terminated
				sig_chld(SIGCHLD);
			}

			if (!create_pipe) {
				create_pipe = true;
				pipe(p_num);
			}

			// read the data to a new pipe, this new pipe may contains mutiple pipes' data
			int read_size;
			while ((read_size = Readn(s.first, buf, 1024)) > 0) {
				Writen(p_num[1], buf, read_size);
			}
			wait_to_erase.push_back(s.first);
		}
		
	}

	for (auto s: wait_to_erase) {
		pipe_num_to.erase(s);
	}

	close(p_num[1]);
}

void update_pipe_num_to() {
	for (auto &s:pipe_num_to) {
		s.second--;
	}
}

void clear_tmp() {
	tmp.argv.clear();
	tmp.pipe_to = 0;
	tmp.store_addr = "";
	tmp.err = false;
	tmp.number_pipe = false;
	tmp.next_line = false;
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
		int cmd_argc = argcs_of_cmd[pid];
		argvs_of_cmd.erase(pid);
		argcs_of_cmd.erase(pid);
		
		free(cmd_argv);
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
	while (nremain > 0) { //&& *(buf_now-1) != '\n'
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
	int	n;

	if ( (n = readn(fid, buf, size)) < 0)
		err_sys("readn error\n");
	return(n);
}
