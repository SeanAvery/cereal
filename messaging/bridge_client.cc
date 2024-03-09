#include <algorithm>
#include <cassert>
#include <csignal>
#include <iostream>
#include <map>
#include <string>

typedef void (*sighandler_t)(int sig);

#include "cereal/services.h"
#include "cereal/messaging/impl_msgq.h"
#include "cereal/messaging/impl_zmq.h"

std::atomic<bool> do_exit = false;
static void set_do_exit(int sig) {
  do_exit = true;
}

void sigpipe_handler(int sig) {
  assert(sig == SIGPIPE);
  std::cout << "SIGPIPE received" << std::endl;
}

static std::vector<std::string> get_services(std::string whitelist_str, bool zmq_to_msgq) {
  std::vector<std::string> service_list;
  for (const auto& it : services) {
    std::string name = it.second.name;
    bool in_whitelist = whitelist_str.find(name) != std::string::npos;
    if (name == "plusFrame" || name == "uiLayoutState" || (zmq_to_msgq && !in_whitelist)) {
      continue;
    }
    service_list.push_back(name);
  }
  return service_list;
}

int main(int argc, char** argv) {
  signal(SIGPIPE, (sighandler_t)sigpipe_handler);
  signal(SIGINT, (sighandler_t)set_do_exit);
  signal(SIGTERM, (sighandler_t)set_do_exit);


  std::string ip = argc > 1 ? argv[1] : "127.0.0.1";
  std::string whitelist_str = argc > 2 ? std::string(argv[2]) : "";

  Poller *poller;
  Context *pub_context;
  Context *sub_context;
  poller = new MSGQPoller();
  pub_context = new ZMQContext();
  sub_context = new MSGQContext();

  std::map<SubSocket*, PubSocket*> sub2pub;
  for (auto endpoint : get_services(whitelist_str, true)) {
    PubSocket * pub_sock;
    SubSocket * sub_sock;

    pub_sock = new ZMQPubSocket();
    sub_sock = new MSGQSubSocket();
    pub_sock->request(pub_context, endpoint, ip, 10, true);
    sub_sock->connect(sub_context, endpoint, "127.0.0.1", false);

    poller->registerSocket(sub_sock);
    sub2pub[sub_sock] = pub_sock;
  }

  while (!do_exit) {
    for (auto sub_sock : poller->poll(100)) {
      Message * msg = sub_sock->receive();
      if (msg == NULL) continue;
      int ret;
      do {
        ret = sub2pub[sub_sock]->sendMessage(msg);
      } while (ret == -1 && errno == EINTR && !do_exit);
      assert(ret >= 0 || do_exit);
      delete msg;

      if (do_exit) break;
    }
  }
  return 0;
}
