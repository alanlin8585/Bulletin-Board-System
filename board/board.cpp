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
#include <signal.h>
#define LISTENQ 128
#define MAXLINE 200000

using namespace std;

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

void content_tran(string &s) {
    int top = 0;
    for (int i = 0; i < (int)s.size(); i++) {
        if (i + 3 < (int)s.size() && s[i] == '<' && s[i + 1] == 'b' && s[i + 2] == 'r' && s[i + 3] == '>') {
            s[top++] = '\n';
            i += 3;
        } else {
            s[top++] = s[i];
        }
    }
    s.resize(top);
}

struct POST {
    bool alive;
    int board_id;
    string author, title, content, date;
    vector<pair<string, string>> comment;
};

int user_cnt, board_cnt, post_cnt;
map<string, int> user, board;
vector<int> user_logined;
vector<pair<string, string>> boardv(1);
vector<POST> post(1);
vector<string> usernamev, passwdv;
vector<vector<int>> board_to_post(1, vector<int>(0));

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

bool check_post(int post_id, int connfd, char *buff) {
    if (post_id <= 0 || post_id > post_cnt || post[post_id].alive == 0) {
        cprint("Post does not exist.\n", connfd, buff);
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
    if (p == user.end() || passwdv[p -> second] != passwd) {
        cprint("Login failed.\n", connfd, buff);
        return;
    }
    user_logined[p -> second] = 1;
    ori_username = username;
    cprint("Welcome, " + username + ".\n", connfd, buff);
}

void _logout(string &username, int connfd, char *buff) {
    if (!check_logined(username, connfd, buff))
        return;
    user_logined[user[username]] = 0;
    cprint("Bye, " + username + ".\n", connfd, buff);
    username.clear();
}

void _create_board(string &username, vector<string> &argv, int connfd, char *buff) {
    if (argv.size() != 2) {
        cprint("Usage: create-board <name>\n", connfd, buff);
        return;
    }
    if (!check_logined(username, connfd, buff))
        return;
    string boardname = argv[1];
    if (board.find(boardname) != board.end()) {
        cprint("Board already exists.\n", connfd, buff);
        return;
    }
    board[boardname] = ++board_cnt;
    boardv.push_back(make_pair(boardname, username));
    board_to_post.push_back(vector<int>(0));
    cprint("Create board successfully.\n", connfd, buff);
}

void _create_post(string &username, vector<string> &argv, int connfd, char *buff) {
    int argc = 0, now = 0;
    string title, content;
    for (int i = 2; i < (int)argv.size(); i++) {
        if (argv[i] == "--title") {
            argc |= 1;
            now = 1;
        } else if (argv[i] == "--content") {
            argc |= 2;
            now = 2;
        } else if (now == 1) {
            if (!title.empty())
                title += ' ';
            title += argv[i];
        } else if (now == 2) {
            if (!content.empty())
                content += ' ';
            content += argv[i];
        }
    }
    if (argc != 3 || title.empty() || content.empty()) {
        cprint("Usage : create-post <board-name> --title <title> --content <content>\n", connfd, buff);
        return;
    }
    if (!check_logined(username, connfd, buff))
        return;
    string boardname = argv[1];
    if (board.find(boardname) == board.end()) {
        cprint("Board does not exist.\n", connfd, buff);
        return;
    }
    int board_id = board[boardname];
    content_tran(content);
    POST p;
    p.alive = 1;
    p.board_id = board_id;
    p.author = username;
    p.title = title;
    p.content = content;

    time_t raw_time;
    tm *timeinfo;
    time(&raw_time);
    timeinfo = localtime(&raw_time);
    p.date = to_string((int)(timeinfo -> tm_mon) + 1) + '/' + to_string(timeinfo -> tm_mday);

    p.comment.clear();

    post_cnt++;
    board_to_post[board_id].push_back(post_cnt);
    post.push_back(p);
    cprint("Create post successfully.\n", connfd, buff);
}

void _list_board(int connfd, char *buff) {
    cprint("Index Name Moderator\n", connfd, buff);
    for (int i = 1; i <= board_cnt; i++)
        cprint(to_string(i) + ' ' + boardv[i].first + ' ' + boardv[i].second + "\n", connfd, buff);
}

void _list_post(vector<string> &argv, int connfd, char *buff) {
    if (argv.size() != 2) {
        cprint("Usage: list-post <board-name>\n", connfd, buff);
        return;
    }
    string boardname = argv[1];
    if (board.find(boardname) == board.end()) {
        cprint("Board does not exist.\n", connfd, buff);
        return;
    }
    int board_id = board[boardname];
    cprint("S/N Title Author Date\n", connfd, buff);
    for (int i : board_to_post[board_id]) {
        cprint(to_string(i) + ' ' + post[i].title + ' ' + post[i].author + ' ' + post[i].date + '\n', connfd, buff);
    }
}

void _read(vector<string> &argv, int connfd, char *buff) {
    if (argv.size() != 2 || is_number(argv[1]) == false) {
        cprint("Usage: read <post-S/N>\n", connfd, buff);
        return;
    }
    int post_id = stoi(argv[1]);
    if (!check_post(post_id, connfd, buff)) {
        return;
    }
    POST &p = post[post_id];
    cprint("Author: " + p.author + '\n', connfd, buff);
    cprint("Title: " + p.title + '\n', connfd, buff);
    cprint("Date: " + p.date + '\n', connfd, buff);
    cprint("--\n", connfd, buff);
    cprint(p.content + '\n', connfd, buff);
    cprint("--\n", connfd, buff);
    for (pair<string, string> &i : p.comment)
        cprint(i.first + ": " + i.second + '\n', connfd, buff);
}

void _delete_post(string &username, vector<string> &argv, int connfd, char *buff) {
    if (argv.size() != 2 || is_number(argv[1]) == false) {
        cprint("Usage: delete-post <post-S/N>\n", connfd, buff);
        return;
    }
    if (!check_logined(username, connfd, buff))
        return;
    int post_id = stoi(argv[1]);
    if (!check_post(post_id, connfd, buff))
        return;
    POST &p = post[post_id];
    if (p.author != username) {
        cprint("Not the post owner.\n", connfd, buff);
        return;
    }
    p.alive = 0;
    vector<int> &post_list = board_to_post[p.board_id];
    for (int i = 0; i < (int)post_list.size(); i++) {
        if (post_list[i] == post_id) {
            post_list.erase(post_list.begin() + i);
            break;
        }
    }
    cprint("Delete successfully.\n", connfd, buff);
}

void _update_post(string &username, vector<string> &argv, int connfd, char *buff) {
    if (argv.size() < 4 || is_number(argv[1]) == false || (argv[2] != "--title" && argv[2] != "--content")) {
        cprint("Usage: update-post <post-S/N> --title/content <new>\n", connfd, buff);
        return;
    }
    if (!check_logined(username, connfd, buff))
        return;
    int post_id = stoi(argv[1]);
    if (!check_post(post_id, connfd, buff))
        return;
    POST &p = post[post_id];
    if (p.author != username) {
        cprint("Not the post owner.\n", connfd, buff);
        return;
    }
    string *edit_ptr;
    if (argv[2] == "--title") {
        edit_ptr = &(p.title);
    } else if (argv[2] == "--content") {
        edit_ptr = &(p.content);
    } else {
        cprint("Usage: update-post <post-S/N> --title/content <new>\n", connfd, buff);
        return;
    }
    edit_ptr -> clear();
    for (int i = 3; i < (int)argv.size(); i++) {
        if (!(edit_ptr -> empty()))
            (*edit_ptr) += ' ';
        (*edit_ptr) += argv[i];
    }
    if (argv[2] == "--content") {
        content_tran(*edit_ptr);
    }
    cprint("Update successfully.\n", connfd, buff);
}

void _comment(string &username, vector<string> &argv, int connfd, char *buff) {
    if (argv.size() < 3 || is_number(argv[1]) == false) {
        cprint("Usage: comment <post-S/N> <comment>\n", connfd, buff);
        return;
    }
    if (!check_logined(username, connfd, buff))
        return;
    int post_id = stoi(argv[1]);
    if (!check_post(post_id, connfd, buff))
        return;
    string comment;
    for (int i = 2; i < (int)argv.size(); i++) {
        if (!comment.empty())
            comment += ' ';
        comment += argv[i];
    }
    post[post_id].comment.push_back(make_pair(username, comment));
    cprint("Comment successfully.\n", connfd, buff);
}

bool do_command(string &username, int connfd, char *buff) {
    string s;
    while (1) {
        if (recv(connfd, buff, 1, 0) <= 0)
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
    } else if (com == "create-board") {
        _create_board(username, argv, connfd, buff);
    } else if (com == "create-post") {
        _create_post(username, argv, connfd, buff);
    } else if (com == "list-board") {
        _list_board(connfd, buff);
    } else if (com == "list-post") {
        _list_post(argv, connfd, buff);
    } else if (com == "read") {
        _read(argv, connfd, buff);
    } else if (com == "delete-post") {
        _delete_post(username, argv, connfd, buff);
    } else if (com == "update-post") {
        _update_post(username, argv, connfd, buff);
    } else if (com == "comment") {
        _comment(username, argv, connfd, buff);
    }
    return true;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        cout << "Usage: ./board <port>" << endl;
        return 0;
    }

    sigaction(SIGPIPE, NULL, NULL);

    int listenport = stoi(argv[1]);
    int connfd, listenfd, sockfd;
    sockaddr_in servaddr, cliaddr;
    socklen_t clilen;
    char buff[MAXLINE];

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == -1) {
        cerr << "Socket create error" << endl;
        return -1;
    }

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(listenport);

    if (bind(listenfd, (sockaddr*)&servaddr, sizeof(servaddr)) == -1) {
        cerr << "Bind error" << endl;
        return -1;
    }

    if (listen(listenfd, LISTENQ) == -1) {
        cerr << "Listen error" << endl;
        return -1;
    }

    vector<int> client(FD_SETSIZE, -1);
    vector<string> connect_user(FD_SETSIZE);
    int mxfd = listenfd;
    int mxcliid = -1;
    fd_set rset, allset;
    FD_ZERO(&allset);
    FD_SET(listenfd, &allset);

    while (true) {
        rset = allset;
        if (select(mxfd + 1, &rset, NULL, NULL, NULL) == -1) {
            cerr << "Select error" << endl;
            return -1;
        }
        if (FD_ISSET(listenfd, &rset)) {
            clilen = sizeof(cliaddr);
            if ((connfd = accept(listenfd, (sockaddr*)&cliaddr, &clilen)) == -1) {
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

            FD_SET(connfd, &allset);
            mxfd = max(mxfd, connfd);
            mxcliid = max(mxcliid, cliid);
        }
        for (int i = 0; i <= mxcliid; i++) {
            if ((sockfd = client[i]) < 0)
                continue;
            if (FD_ISSET(sockfd, &rset)) {
                string username = connect_user[i];
                if (do_command(username, sockfd, buff)) {
                    wait_command(sockfd, buff);
                    connect_user[i] = username;
                } else {
                    if (!username.empty())
                        user_logined[user[username]] = 0;
                    close(sockfd);
                    FD_CLR(sockfd, &allset);
                    client[i] = -1;
                }
            }
        }
    }
}

