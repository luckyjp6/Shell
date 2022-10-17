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

using namespace std;

struct my_cmd{
	vector<string> argv;
	int pipe_to = 0;
	string store_addr = "";
	bool err = false;
	bool number_pipe = false;
};

struct args{
	char **argv;
	int argc;
	bool number_pipe;
};

vector<my_cmd> C;
map< size_t, args> args_of_cmd;
map< size_t, int > pipe_num_to; // pipe_num, counter
my_cmd tmp;

void err_sys(const char* x);
void sig_chld(int signo);
void wait_all_children();
void conditional_wait();

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
	string s;
	while(should_run){
		if(cin.eof()) break;

		printf("%% "); fflush(stdout);
		
		string buf = "";
		getline(cin, buf);
		int input_length = buf.size();

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
			if (i % 50 == 0 && i != 0) wait_all_children();
			if (C[i].argv[0] == "exit") {
				update_pipe_num_to();
				C.clear();
				should_run = false;
				break;
			}
			if (C[i].argv[0] == "setenv") {
				update_pipe_num_to();
				if (C[i].argv.size() != 3) {
					char err[] = "Usage: setenv [Variable] [Value].\n";
					Writen(STDERR_FILENO, err, sizeof(err));
					continue;
				}
				setenv(C[i].argv[1].data(), C[i].argv[2].data(), 1);
				continue;
			}
			if (C[i].argv[0] == "printenv") {
				update_pipe_num_to();
				if (C[i].argv.size() != 2) {
					char err[] = "Usage: printenv [Variable].\n";
					Writen(STDERR_FILENO, err, sizeof(err));
					continue;
				}
				char* env_info = getenv(C[i].argv[1].data());
				if (env_info != NULL) printf("%s\n", env_info);
				continue;
			}
			
			args cmd;
			cmd.argc = C[i].argv.size();
			cmd.argv = (char**) malloc(sizeof(char*) * (cmd.argc+1));
			cmd.number_pipe = C[i].number_pipe;

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
				char err[] = "failed to fork\n";
				Writen(STDERR_FILENO, err, sizeof(err));
				return -1;
			}

			// parent do
			if (pid > 0) {
				if (need_pipe) close(p_num[1]);
				if (need_data) close(data_pipe[0]);

				// record memory allocate address
				args_of_cmd.insert(pair<size_t, args> (pid, cmd));
				
				// update when new line
				if (C[i].number_pipe || i == C.size()-1) update_pipe_num_to();
				
				// store read id of pipe
				if (C[i].number_pipe) pipe_num_to.insert(pair<size_t, int>(p_num[0], C[i].pipe_to-1));
				else if (need_pipe) pipe_num_to.insert(pair<size_t, int>(p_num[0], 0));

				// process data from multiple pipe
				if (need_data && data_list.size() != 0) {
					// fork a child to process
					pid = fork();
					if (pid < 0) {
						char err[] = "failed to fork\n";
						Writen(STDERR_FILENO, err, sizeof(err));
						return -1;
					}

					if (pid == 0) {
						// read data from each pipe (FIFO)
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
						for(auto id: data_list) close(id);
						data_list.clear();
						
						// information for signal handler about how to handle this child
						cmd.argv = NULL; cmd.argc = 0; cmd.number_pipe = false;
						args_of_cmd.insert(pair<size_t, args> (pid, cmd));
					}
				}

				continue;
			}
			
			// child do
					
			// received data from other process
			if (need_data){
				if (data_list.size() != 0) {
					close(data_pipe[1]);
					for(auto id: data_list) close(id);
					data_list.clear();
				}
				dup2(data_pipe[0], STDIN_FILENO);
			}
			
			// pipe stderr
			if (C[i].err) dup2(p_num[1], STDERR_FILENO);
			
			// pipe data to other process
			if (need_pipe) {
				close(p_num[0]);
				dup2(p_num[1], STDOUT_FILENO);
			}

			// store data to a file
			if (C[i].store_addr.size() != 0) {
				size_t file_id = open(C[i].store_addr.data(), O_WRONLY|O_CREAT|O_TRUNC, 00777);
				// clear the file content
				ftruncate(file_id, 0);
				lseek(file_id, 0, SEEK_SET);
				if (file_id < 0) {
					string err_s = "Failed to open file" + C[i].store_addr + "\n";
					char err[err_s.size()];
					strcpy(err, err_s.c_str());
					Writen(STDERR_FILENO, err, sizeof(err));
					free(cmd.argv);
					continue;
				}
				dup2(file_id, STDOUT_FILENO);
			}

			// get the arguments ready
			for (int j = 0; j < cmd.argc; j++){
				cmd.argv[j] = (char*) malloc(sizeof(char) * C[i].argv[j].size()+1);
				strcpy(cmd.argv[j], C[i].argv[j].c_str());
			}
			cmd.argv[cmd.argc] = new char;
			cmd.argv[cmd.argc] = NULL;

			// exec!!!!
			if (execvp(cmd.argv[0], cmd.argv) < 0) {
				string err_s = cmd.argv[0];
				err_s = "Unknown command: [" + err_s + "].\n";
				char err[err_s.size()];
				strcpy(err, err_s.c_str());
				Writen(STDERR_FILENO, err, sizeof(err));

				// close pipe
				if (need_data) close(data_pipe[0]);
				if (need_pipe) close(p_num[1]);
				return -1;
			}
		}

		// wait for all children, except for command with number pipe
		conditional_wait();
		C.clear();
	}
	
	// wait for all children
	wait_all_children();
	return 0;
}

void check_need_data(bool &need, int (&data_pipe)[2], vector<int> &data_list) {
	data_list.clear();

	// counter == 0 => pipe the data to the command that execute next
	for (auto s: pipe_num_to) {
		if (s.second == 0) data_list.push_back(s.first);		
	}

	need = (data_list.size() > 0);
	if (!need) return;

	// if only one pipe of data needed
	// directly assign read p_id to data_pipe
	if(data_list.size() == 1) {
		data_pipe[0] = data_list[0];
		pipe_num_to.erase(data_list[0]);
		data_list.clear();
		return;
	}

	// parent will tackle the data from multiple pipe
	pipe(data_pipe);
	for (auto s: data_list) {
		pipe_num_to.erase(s);
	}
}

void update_pipe_num_to() { 
	vector<int> wait_to_erase;
	// update pipe counter
	for (auto &s: pipe_num_to) {
		s.second--;
		if (s.second < 0) wait_to_erase.push_back(s.first);
	}

	// erase unnecessary pipe id
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
	// only process number pipe
	tmp.number_pipe = true;
	if (s[0] == '!') tmp.err = true;
	for (int i = 1; i < s.size(); i++)
		tmp.pipe_to = tmp.pipe_to *10 + (int)(s[i] - '0');
}

void wait_all_children() {
	while (!args_of_cmd.empty()) {
		sig_chld(SIGCHLD);			
	}
}

void conditional_wait() {
	while(!args_of_cmd.empty()) {
		sig_chld(SIGCHLD);

		// stop waiting if remaining are all commands with number pipe
		// the output are not immediately needed
		bool must_wait = false;
		for (auto s:args_of_cmd) {
			if (s.second.number_pipe == false) {
				must_wait = true;
				break;
			}
		}
		if (!must_wait) break;
	}
}

void sig_chld(int signo)
{
	int	pid, stat;

	while ( (pid = waitpid(-1, &stat, WNOHANG)) > 0){
		
		// free the memory!!
		char **cmd_argv = args_of_cmd[pid].argv;
		args_of_cmd.erase(pid);
		
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
			if (nwritten < 0 && errno == EINTR) nwritten = 0; // the error is not from write()
			else return -1; // write error
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
			if (errno == EINTR) nread = 0; // the error is not from read
			else return -1; // read error
		} 
		else if (nread == 0) break; // EOF

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
