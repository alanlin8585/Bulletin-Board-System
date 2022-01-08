#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <vector>
#include <map>
#include <queue>
#include <set>
#include <pthread.h>
#define LISTENQ 128
#define MAXLINE 200000

using namespace std;

queue<int> ended_thread;

void cprint(string s, int connfd, char *buff) {
    snprintf(buff, sizeof(char) * MAXLINE, "%s", s.c_str());
    write(connfd, buff, strlen(buff));  
}

void welcome(int connfd, char buff[]) {
    cprint("********************************\n", connfd, buff);
    cprint("** Welcome to the BBS server. **\n", connfd, buff);
    cprint("********************************\n", connfd, buff);
}

int user_cnt;
map<string, int> user;
vector<string> usernamev, passwdv;
vector<map<string, queue<string>>> msg;

bool check_logined(string &username, int connfd, char *buff) {
    if (username.empty()) {
        cprint("Please login first.\n", connfd, buff);
        return false;
    }
    return true;
}

bool check_logouted(string &username, int connfd, char *buff) {
    if (!username.empty()) {
        cprint("Please logout first.\n", connfd, buff);
        return false;
    }
    return true;
}

void _register(vector<string> &argv, int connfd, char *buff) {
    if (argv.size() != 3) {
        cprint("Usage: register <username> <password>\n", connfd, buff);
        return;
    }
    string username = argv[1];
    string passwd = argv[2];
    if (user.find(username) != user.end()) {
        cprint("Username is already used.\n", connfd, buff);
        return;
    }
    cprint("Register successfully.\n", connfd, buff);
    user[username] = user_cnt++;
    passwdv.push_back(passwd);
    usernamev.push_back(username);
    msg.push_back(map<string, queue<string>>());
}

void _login(string &ori_username, vector<string> &argv, int connfd, char *buff) {
    if (argv.size() != 3) {
        cprint("Usage: login <username> <password>\n", connfd, buff);
        return;
    }
    if (!check_logouted(ori_username, connfd, buff)) {
        return;
    }
    string username = argv[1];
    string passwd = argv[2];
    auto p = user.find(username);
    if (p == user.end() || passwdv[p -> second] != passwd) {
        cprint("Login failed.\n", connfd, buff);
        return;
    }
    ori_username = username;
    cprint("Welcome, " + username + ".\n", connfd, buff);
}

void _logout(string &username, int connfd, char *buff) {
    if (!check_logined(username, connfd, buff)) {
        return;
    }
    cprint("Bye, " + username + ".\n", connfd, buff);
    username.clear();
}

void _whoami(string &username, int connfd, char *buff) {
    if (!check_logined(username, connfd, buff)) {
        return;
    }
    cprint(username + "\n", connfd, buff);
}

void _list_user(int connfd, char *buff) {
    for (auto &i : user) {
        cprint(i.first + "\n", connfd, buff);
    }
}

void _send(string &username, vector<string> &argv, int connfd, char *buff) {
    if (argv.size() != 3) {
        cprint("Usage: send <username> <message>\n", connfd, buff);
        return;
    }
    if (!check_logined(username, connfd, buff)) {
        return;
    }
    string send_username = argv[1];
    auto p = user.find(send_username);
    if (p == user.end()) {
        cprint("User not existed.\n", connfd, buff);
        return;
    }
    msg[p -> second][username].push(argv[2]);
}

void _list_msg(string &username, int connfd, char *buff) {
    if (!check_logined(username, connfd, buff)) {
        return;
    }
    int user_id = user[username];
    if (msg[user_id].empty()) {
        cprint("Your message box is empty.\n", connfd, buff);
        return;
    }
    for (auto &i : msg[user_id]) {
        cprint(to_string(i.second.size()) + " message from " + i.first + ".\n", connfd, buff);
    }
}

void _receive(string &username, vector<string> &argv, int connfd, char *buff) {
    if (argv.size() != 2) {
        cprint("Usage: receive <username>\n", connfd, buff);
        return;
    }
    if (!check_logined(username, connfd, buff)) {
        return;
    }
    string receive_username = argv[1];
    auto p = user.find(receive_username);
    if (p == user.end()) {
        cprint("User not existed.\n", connfd, buff);
        return;
    }
    int user_id = user[username];
    auto qp = msg[user_id].find(receive_username);
    if (qp != msg[user_id].end()) {
        cprint(qp -> second.front() + "\n", connfd, buff);
        qp -> second.pop();
        if (qp -> second.empty()) {
            msg[user_id].erase(receive_username);
        }
    }
}

bool command(string &username, int connfd, char *buff) {
    cprint("% ", connfd, buff);
    string s;
    while (1) {
        if (read(connfd, buff, 1) <= 0)
            return false;
        else if (buff[0] != '\n')
            s.push_back(buff[0]);
        else
            break;
    }
    vector<string> argv(1, "");
    
    int msgflag = 0;
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.pop_back();
    for (char c : s) {
        if (c == '\"') {
            msgflag++;
        } else if (!(msgflag & 1) && (c == ' ' || c == '\t')) {
            if (!argv.back().empty())
                argv.push_back("");
        } else {
            argv.back().push_back(c);
        }
    }
    if (argv.empty()) {
        return true;
    }
    string com = argv[0];
    if (com == "exit") {
        if (!username.empty())
            _logout(username, connfd, buff);
        return false;
    } else if (com == "register") {
        _register(argv, connfd, buff);
    } else if (com == "login") {
        _login(username, argv, connfd, buff);
        return true;
    } else if (com == "logout") {
        _logout(username, connfd, buff);
    } else if (com == "whoami") {
        _whoami(username, connfd, buff);
    } else if (com == "list-user") {
        _list_user(connfd, buff);
    } else if (com == "send") {
        _send(username, argv, connfd, buff);
    } else if (com == "list-msg") {
        _list_msg(username, connfd, buff);
    } else if (com == "receive") {
        _receive(username, argv, connfd, buff);
    }
    return true;
}

void* client(void *p) {
    char *buff = new char[MAXLINE];
    memset(buff, 0, MAXLINE);
    pair<int, int> data = *(pair<int, int>*)p;
    int connfd = data.first;
    int tid = data.second;
    string username;
    welcome(connfd, buff);
    while (command(username, connfd, buff));
    
    close(connfd);
    ended_thread.push(tid);
    
    delete buff;
    pthread_exit(0);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        cout << "Usage: ./mail <port>" << endl;
        return 0;
    }
    char *buff = new char[MAXLINE];
    int connfd;
    socklen_t len;
    sockaddr_in servaddr, cliaddr;

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(atoi(argv[1]));

    bind(listenfd, (sockaddr *) &servaddr, sizeof(servaddr));

    listen(listenfd, LISTENQ);

    pthread_t thread_id;
    int tid = 0;
    vector<pthread_t> threadv;
    while (1) {
        len = sizeof(cliaddr);
        connfd = accept(listenfd, (sockaddr *) &cliaddr, &len);
        pair<int, int> datas = make_pair(connfd, tid++);
        cout << "connection from ";
        cout << inet_ntop(AF_INET, &cliaddr.sin_addr, buff, sizeof(char) * MAXLINE);
        cout << ", port " << ntohs(cliaddr.sin_port) << "\n";
        if (pthread_create( &thread_id, NULL, client, (void*) &datas) < 0) {
            perror("cant create thread");
            return -1;
        }
        threadv.push_back(thread_id);
        while (!ended_thread.empty()) {
            int now = ended_thread.front();
            ended_thread.pop();
            if (pthread_join(threadv[now], NULL) != 0) {
                cerr << "thread join error" << endl;
                return -1;
            }
        }
    }
    delete buff;
}
