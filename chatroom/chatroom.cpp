#include <iostream>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <vector>
#include <time.h>
#include <map>
#include <set>
#include <signal.h>
#define LISTENQ 128
#define MAXLINE 200000
#define MAXPORT 65535

using namespace std;

struct version1_a {
    unsigned char flag;
    unsigned char version;
    unsigned char payload[0];
} __attribute__((packed));

struct version1_b {
    unsigned short len;
    unsigned char data[0];
} __attribute__((packed));

void pack_version1(string &name, string &msg, char *buff, int &len) {
    uint16_t name_len = (uint16_t)name.size();
    uint16_t msg_len = (uint16_t)msg.size();
    struct version1_a *pa = (struct version1_a*) buff;
    struct version1_b *pb1 = (struct version1_b*) (buff + sizeof(struct version1_a));
    struct version1_b *pb2 = (struct version1_b*) (buff + sizeof(struct version1_a) + sizeof(struct version1_b) + name_len);
    len = sizeof(struct version1_a) + sizeof(struct version1_b) * 2 + name_len + msg_len;
    pa->flag = 0x01;
    pa->version = 0x01;
    pb1->len = htons(name_len);
    memcpy(pb1->data, name.c_str(), name_len);
    pb2->len = htons(msg_len);
    memcpy(pb2->data, msg.c_str(), msg_len);
}

int transfer(char c) {
    if ('A' <= c && c <= 'Z') {
        return c - 'A';
    } else if ('a' <= c && c <= 'z') {
        return c - 'a' + 26;
    } else if ('0' <= c && c <= 'z') {
        return c - '0' + 52;
    } else if (c == '+') {
        return 62;
    } else if (c == '/') {
        return 63;
    }
    return 0;
}

const char base64_table[100] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void base64_decode(string &s) {
    string rs;
    int cnt = 7, tmp;
    char now = 0;
    for (char c : s) {
        if (c == '=')
            continue;
        tmp = transfer(c);
        for (int i = 5; i >= 0; i--) {
            if (tmp & (1 << i))
                now |= (1 << cnt);
            if (cnt-- == 0) {
                if (now != 0)
                    rs.push_back(now);
                cnt = 7;
                now = 0;
            }
        }
    }
    s.swap(rs);
}

void base64_encode(string &s) {
    string rs;
    int cnt = 5, now = 0;
    for (char c : s) {
        for (int i = 7; i >= 0; i--) {
            if (c & (1 << i))
                now |= (1 << cnt);
            if (cnt-- == 0) {
                rs.push_back(base64_table[now]);
                cnt = 5;
                now = 0;
            }
        }
    }
    if (cnt != 5)
        rs.push_back(base64_table[now]);
    while (rs.size() % 4 != 0)
        rs.push_back('=');
    s.swap(rs);
}

void cprint(string s, int connfd, char *buff) {
    snprintf(buff, sizeof(char) * MAXLINE, "%s", s.c_str());
    write(connfd, buff, strlen(buff));
}

void wait_command(int connfd, char buff[]) {
    cprint("% ", connfd, buff);
}

void welcome(int connfd, char buff[]) {
    cprint("********************************\n", connfd, buff);
    cprint("** Welcome to the BBS server. **\n", connfd, buff);
    cprint("********************************\n", connfd, buff);
}

bool cgetmsg(string &name, string &msg, int udp_listenfd, char *buff) {
    name.clear();
    msg.clear();
    int top = 0;
    char flag, version;
    if (recv(udp_listenfd, buff, MAXLINE, 0) <= 0)
        return false;

    flag = buff[top++];
    version = buff[top++];

    if (flag != 1) {
        return false;
    }
    if (version == 1) {
        for (int i = 0, len; i < 2; i++) {
            len = ((int)buff[top]) << 8 | ((int)buff[top + 1]);
            top += 2;
            if (i == 0) {
                name.resize(len);
                for (int j = 0; j < len; j++)
                    name[j] = buff[top++];
            } else {
                msg.resize(len);
                for (int j = 0; j < len; j++)
                    msg[j] = buff[top++];
            }
        }
    } else if (version == 2) {
        while (buff[top] != '\n')
            name.push_back(buff[top++]);
        base64_decode(name);
        top++;
        while (buff[top] != '\n')
            msg.push_back(buff[top++]);
        base64_decode(msg);
    } else { // error
        return false;
    }
    return true;
}

string chat_history;
map<pair<int, int>, pair<sockaddr_in, socklen_t>> active_udp_user;
bool cprintmsg(string &name, string &msg, int connfd, char *buff) {
    chat_history += name;
    chat_history += ':';
    chat_history += msg;
    chat_history += '\n';
    int len;
    pack_version1(name, msg, buff, len);
    for (const pair<pair<int, int>, pair<sockaddr_in, socklen_t>> &i : active_udp_user) { // version 1
        if (i.first.second == 1) {
            if (sendto(connfd, buff, len, 0, (sockaddr*)&i.second.first, sizeof(i.second.first)) == -1) {
                return false;
            }
        }
    }
    base64_encode(name);
    base64_encode(msg);
    len = sprintf(buff, "\x01\x02%s\n%s\n", name.c_str(), msg.c_str());
    for (const pair<pair<int, int>, pair<sockaddr_in, socklen_t>> &i : active_udp_user) { // version 2
        if (i.first.second == 2) {
            if (sendto(connfd, buff, len, 0, (sockaddr*)&i.second.first, i.second.second) == -1) {
                return false;
            }
        }
    }
    return true;
}

int user_cnt;
map<string, int> user;
set<string> blacklist;
vector<int> user_logined;
vector<string> usernamev, passwdv;

map<string, int> filter_cnt;
const int FILTER_MAX = 9;
const string filter_list[FILTER_MAX] = {"how", "you", "or", "pek0", "tea", "ha", "kon", "pain", "Starburst Stream"};
int filter_fail[FILTER_MAX][64];

void make_fail() {
    for (int i = 0; i < FILTER_MAX; i++) {
        int now = -1;
        filter_fail[i][0] = -1;
        for (int j = 1; j < (int)filter_list[i].size(); j++) {
            while (now != -1 && filter_list[i][now + 1] != filter_list[i][j])
                now = filter_fail[i][now];
            if (filter_list[i][now + 1] == filter_list[i][j])
                now++;
            filter_fail[i][j] = now;
        }
    }
}

void filter(string &username, string &msg) {
    int flag = 0;
    for (int i = 0; i < FILTER_MAX; i++) {
        int now = -1;
        for (int j = 0; j < (int)msg.size(); j++) {
            while (now != -1 && filter_list[i][now + 1] != msg[j])
                now = filter_fail[i][now];
            if (filter_list[i][now + 1] == msg[j])
                now++;
            if (now == (int)filter_list[i].size() - 1) {
                flag = 1;
                now = -1;
                for (int k = j - (int)filter_list[i].size() + 1; k <= j; k++)
                    msg[k] = '*';
            }
        }
    }
    if (flag == 1)
        filter_cnt[username]++;
}

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

bool is_number(string &s) {
    for (int i = 0; i < (int)s.size(); i++) {
        if (s[i] == '-' && i)
            return false;
        else if (!(s[i] >= '0' && s[i] <= '9'))
            return false;
    }
    return true;
}

bool check_port(string &s) {
    if (!is_number(s))
        return false;
    int port = stoi(s);
    return (1 <= port && port <= MAXPORT);
}

bool check_version(string &s) {
    if (!is_number(s))
        return false;
    int version = stoi(s);
    return (version == 1 || version == 2);
}

void make_udp_connection(int &udp_port, int &version, sockaddr_in &cli_from, socklen_t &cli_addrlen) {
    cli_from.sin_family = AF_INET;
    cli_from.sin_port = htons(udp_port);
    cli_addrlen = sizeof(cli_from);
    active_udp_user[make_pair(udp_port, version)] = make_pair(cli_from, cli_addrlen);
}

void break_udp_connection(int &udp_port, int &version) {
    active_udp_user.erase(make_pair(udp_port, version));
    udp_port = version = 0;
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
    user_logined.push_back(0);
    passwdv.push_back(passwd);
    usernamev.push_back(username);
}

void _login(string &ori_username, vector<string> &argv, int connfd, char *buff) {
    if (argv.size() != 3) {
        cprint("Usage: login <username> <password>\n", connfd, buff);
        return;
    }
    if (!check_logouted(ori_username, connfd, buff))
        return;
    string username = argv[1];
    string passwd = argv[2];
    auto p = user.find(username);
    if (p != user.end() && user_logined[p -> second] == 1) {
        cprint("Please logout first.\n", connfd, buff);
        return;
    }
    if (p == user.end()) {
        cprint("Login failed.\n", connfd, buff);
        return;
    }
    if (blacklist.find(username) != blacklist.end()) {
        cprint("We don't welcome " + username + "!\n", connfd, buff);
        return;
    }
    if (passwdv[p -> second] != passwd) {
        cprint("Login failed.\n", connfd, buff);
        return;
    }
    user_logined[p -> second] = 1;
    ori_username = username;
    cprint("Welcome, " + username + ".\n", connfd, buff);
}

void _logout(string &username, vector<string> &argv, int &udp_port, int &version, int connfd, char *buff) {
    if (argv.size() != 1) {
        cprint("Usage: logout\n", connfd, buff);
        return;
    }
    if (!check_logined(username, connfd, buff))
        return;
    if (udp_port != 0)
        break_udp_connection(udp_port, version);
    user_logined[user[username]] = 0;
    cprint("Bye, " + username + ".\n", connfd, buff);
    username.clear();
}

bool _exit(string &username, vector<string> &argv, int &udp_port, int &version, int connfd, char *buff) {
    if (argv.size() != 1) {
        cprint("Usage: exit\n", connfd, buff);
        return false;
    }
    if (!username.empty())
        _logout(username, argv, udp_port, version, connfd, buff);
    return true;
}

void _enter_chat_room(string &username, int &udp_port, int &version, sockaddr_in &cli_from, socklen_t &cli_addrlen, vector<string> &argv, int connfd, char *buff) {
    if (argv.size() != 3) {
        cprint("Usage: enter-chat-room <port> <version>\n", connfd, buff);
        return;
    }
    if (!check_port(argv[1])) {
        cprint("Port " + argv[1] + " is not valid.\n", connfd, buff);
        return;
    }
    if (!check_version(argv[2])) {
        cprint("Version " + argv[2] + " is not supported.\n", connfd, buff);
        return;
    }
    if (!check_logined(username, connfd, buff))
        return;
    if (udp_port != 0)
        break_udp_connection(udp_port, version);
    udp_port = stoi(argv[1]);
    version = stoi(argv[2]);
    make_udp_connection(udp_port, version, cli_from, cli_addrlen);
    cprint("Welcome to public chat room.\nPort:" + argv[1] + "\nVersion:" + argv[2] + "\n" + chat_history, connfd, buff);
}

bool do_command(string &username, int &udp_port, int &version, sockaddr_in *cli_from, int connfd, char *buff) {
    socklen_t cli_addrlen = sizeof(cli_from);
    string s;
    while (1) {
        if (recvfrom(connfd, buff, 1, 0, (sockaddr*)cli_from, &cli_addrlen) <= 0)
            return false;
        else if (buff[0] != '\n')
            s.push_back(buff[0]);
        else
            break;
    }
    vector<string> argv(1, "");

    for (char c : s) {
        if (c == ' ' || c == '\t') {
            if (!argv.back().empty())
                argv.push_back("");
        } else {
            argv.back().push_back(c);
        }
    }
    if (argv.back().empty())
        argv.pop_back();
    if (argv.empty())
        return true;

    string com = argv[0];
    if (com == "exit") {
        if (_exit(username, argv, udp_port, version, connfd, buff))
            return false;
    } else if (com == "register") {
        _register(argv, connfd, buff);
    } else if (com == "login") {
        _login(username, argv, connfd, buff);
        return true;
    } else if (com == "logout") {
        _logout(username, argv, udp_port, version, connfd, buff);
    } else if (com == "enter-chat-room") {
        _enter_chat_room(username, udp_port, version, *cli_from, cli_addrlen, argv, connfd, buff);
    }
    return true;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        cout << "Usage: ./chat <port>" << endl;
        return 0;
    }

    sigaction(SIGPIPE, NULL, NULL);
    make_fail();

    int listenport = stoi(argv[1]);
    int connfd, tcp_listenfd, udp_listenfd, sockfd;
    const int on = 1;
    sockaddr_in servaddr, cliaddr;
    socklen_t clilen;
    char buff[MAXLINE];

    // tcp
    tcp_listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_listenfd == -1) {
        cerr << "TCP socket create error" << endl;
        return -1;
    }

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(listenport);

    if (setsockopt(tcp_listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {
        cerr << "Set socket option error" << endl;
        return -1;
    }

    if (bind(tcp_listenfd, (sockaddr*)&servaddr, sizeof(servaddr)) == -1) {
        cerr << "TCP bind error" << endl;
        return -1;
    }

    if (listen(tcp_listenfd, LISTENQ) == -1) {
        cerr << "Listen error" << endl;
        return -1;
    }

    // udp
    udp_listenfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_listenfd == -1) {
        cerr << "UDP socket create error" << endl;
        return -1;
    }

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(listenport);

    if (bind(udp_listenfd, (sockaddr*)&servaddr, sizeof(servaddr)) == -1) {
        cerr << "UDP bind error" << endl;
        return -1;
    }

    vector<int> client(FD_SETSIZE, -1);
    vector<string> connect_user(FD_SETSIZE);
    vector<int> connect_udp_port(FD_SETSIZE);
    vector<int> connect_version(FD_SETSIZE);
    vector<sockaddr_in> connect_sockaddr(FD_SETSIZE);
    int mxfd = max(tcp_listenfd, udp_listenfd);
    int mxcliid = -1;
    fd_set rset, allset;
    FD_ZERO(&allset);
    FD_SET(tcp_listenfd, &allset);
    FD_SET(udp_listenfd, &allset);

    while (true) {
        rset = allset;
        if (select(mxfd + 1, &rset, NULL, NULL, NULL) == -1) {
            cerr << "Select error" << endl;
            return -1;
        }
        if (FD_ISSET(tcp_listenfd, &rset)) {
            clilen = sizeof(cliaddr);
            if ((connfd = accept(tcp_listenfd, (sockaddr*)&cliaddr, &clilen)) == -1) {
                cerr << "Accept error" << endl;
                return -1;
            }

            cout << "new client: " << inet_ntop(AF_INET, &cliaddr.sin_addr, buff, sizeof(buff)) << " port: " << ntohs(cliaddr.sin_port) << endl;

            int cliid = 0;
            while (cliid < FD_SETSIZE && client[cliid] != -1)
                cliid++;

            if (cliid == FD_SETSIZE) {
                cerr << "too many clients" << endl;
                return -1;
            }

            welcome(connfd, buff);
            wait_command(connfd, buff);
            client[cliid] = connfd;
            connect_user[cliid] = "";
            connect_udp_port[cliid] = 0;
            connect_version[cliid] = 0;

            FD_SET(connfd, &allset);
            mxfd = max(mxfd, connfd);
            mxcliid = max(mxcliid, cliid);
        }
        if (FD_ISSET(udp_listenfd, &rset)) {
            string name, msg;
            if (cgetmsg(name, msg, udp_listenfd, buff)) {
                if (blacklist.find(name) == blacklist.end()) {
                    filter(name, msg);
                    if (filter_cnt[name] >= 3) {
                        blacklist.insert(name);
                        for (int i = 0; i <= mxcliid; i++) {
                            if ((sockfd = client[i]) < 0)
                                continue;
                            else if(connect_user[i] == name) {
                                vector<string> tmp_argv(1, "logout");
                                _logout(connect_user[i], tmp_argv, connect_udp_port[i], connect_version[i], sockfd, buff);
                                wait_command(sockfd, buff);
                            }
                        }
                    }
                    cprintmsg(name, msg, udp_listenfd, buff);
                }
            } else {
                cerr << "UDP packet error" << endl;
                return -1;
            }
        }
        for (int i = 0; i <= mxcliid; i++) {
            if ((sockfd = client[i]) < 0)
                continue;
            if (FD_ISSET(sockfd, &rset)) {
                string username = connect_user[i];
                int udp_port = connect_udp_port[i];
                int version = connect_version[i];
                sockaddr_in *sockaddr = &(connect_sockaddr[i]);
                if (do_command(username, udp_port, version, sockaddr, sockfd, buff)) {
                    wait_command(sockfd, buff);
                    connect_user[i] = username;
                    connect_udp_port[i] = udp_port;
                    connect_version[i] = version;
                } else {
                    if (!username.empty())
                        user_logined[user[username]] = 0;
                    if (connect_udp_port[i] != 0)
                        break_udp_connection(connect_udp_port[i], connect_version[i]);
                    close(sockfd);
                    FD_CLR(sockfd, &allset);
                    client[i] = -1;
                }
            }
        }
    }
}

