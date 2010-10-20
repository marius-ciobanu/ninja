#include <algorithm>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include <assert.h>

using namespace std;

struct Node;
struct FileStat {
  FileStat(const string& path) : path_(path), mtime_(0), node_(NULL) {}
  void Touch(int mtime);
  // Return true if the file exists (mtime_ got a value).
  bool Stat();

  string path_;
  time_t mtime_;
  Node* node_;
};

struct Edge;
struct Node {
  Node(FileStat* file) : file_(file), dirty_(false), in_edge_(NULL) {}

  bool dirty() const { return dirty_; }
  void MarkDirty();

  FileStat* file_;
  bool dirty_;
  Edge* in_edge_;
  vector<Edge*> out_edges_;
};

struct EvalString {
  struct Env {
    virtual string Evaluate(const string& var) = 0;
  };
  bool Parse(const string& input);
  string Evaluate(Env* env);

  const string& unparsed() const { return unparsed_; }

  string unparsed_;
  enum TokenType { RAW, SPECIAL };
  typedef vector<pair<string, TokenType> > TokenList;
  TokenList parsed_;
};

bool EvalString::Parse(const string& input) {
  unparsed_ = input;

  string::size_type start, end;
  start = 0;
  do {
    end = input.find_first_of("@$", start);
    if (end == string::npos) {
      end = input.size();
      break;
    }
    if (end > start)
      parsed_.push_back(make_pair(input.substr(start, end - start), RAW));
    start = end;
    for (end = start + 1; end < input.size(); ++end) {
      if (!('a' <= input[end] && input[end] <= 'z'))
        break;
    }
    if (end == start + 1) {
      // XXX report bad parse here
      return false;
    }
    parsed_.push_back(make_pair(input.substr(start, end - start), SPECIAL));
    start = end;
  } while (end < input.size());
  if (end > start)
    parsed_.push_back(make_pair(input.substr(start, end - start), RAW));

  return true;
}

string EvalString::Evaluate(Env* env) {
  string result;
  for (TokenList::iterator i = parsed_.begin(); i != parsed_.end(); ++i) {
    if (i->second == RAW)
      result.append(i->first);
    else
      result.append(env->Evaluate(i->first));
  }
  return result;
}

struct Rule {
  Rule(const string& name, const string& command) :
    name_(name) {
    assert(command_.Parse(command));  // XXX
  }
  string name_;
  EvalString command_;
};

struct Edge {
  Edge() : rule_(NULL), env_(NULL) {}

  void MarkDirty(Node* node);
  void RecomputeDirty();
  string EvaluateCommand();  // XXX move to env, take env ptr

  Rule* rule_;
  enum InOut { IN, OUT };
  vector<Node*> inputs_;
  vector<Node*> outputs_;
  EvalString::Env* env_;
};

void FileStat::Touch(int mtime) {
  if (node_)
    node_->MarkDirty();
}

#include <errno.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

bool FileStat::Stat() {
  struct stat st;
  if (stat(path_.c_str(), &st) < 0) {
    if (errno == ENOENT) {
      st.st_mtime = 0;
      return false;
    } else {
      fprintf(stderr, "stat(%s): %s\n", path_.c_str(), strerror(errno));
      return false;
    }
  }

  mtime_ = st.st_mtime;
  return true;
}

void Node::MarkDirty() {
  if (dirty_)
    return;  // We already know.

  if (in_edge_)  // No input edges means never dirty.
    dirty_ = true;
  for (vector<Edge*>::iterator i = out_edges_.begin(); i != out_edges_.end(); ++i)
    (*i)->MarkDirty(this);
}

void Edge::RecomputeDirty() {
  assert(!outputs_.empty());

  time_t min_mtime = outputs_[0]->file_->mtime_;
  for (vector<Node*>::iterator i = outputs_.begin(); i != outputs_.end(); ++i) {
    min_mtime = min(min_mtime, (*i)->file_->mtime_);
  }

  for (vector<Node*>::iterator i = inputs_.begin(); i != inputs_.end(); ++i) {
    if ((*i)->file_->mtime_ > min_mtime) {
      MarkDirty(*i);
      break;
    }
  }
}

void Edge::MarkDirty(Node* node) {
  vector<Node*>::iterator i = find(inputs_.begin(), inputs_.end(), node);
  if (i == inputs_.end())
    return;
  for (i = outputs_.begin(); i != outputs_.end(); ++i)
    (*i)->MarkDirty();
}

struct EdgeEnv : public EvalString::Env {
  EdgeEnv(Edge* edge) : edge_(edge) {}
  virtual string Evaluate(const string& var) {
    string result;
    if (var == "@in") {
      for (vector<Node*>::iterator i = edge_->inputs_.begin();
           i != edge_->inputs_.end(); ++i) {
        if (!result.empty())
          result.push_back(' ');
        result.append((*i)->file_->path_);
      }
    } else if (var == "$out") {
      result = edge_->outputs_[0]->file_->path_;
    } else if (edge_->env_) {
      return edge_->env_->Evaluate(var);
    }
    return result;
  }
  Edge* edge_;
};

string Edge::EvaluateCommand() {
  EdgeEnv env(this);
  return rule_->command_.Evaluate(&env);
}

struct StatCache {
  typedef map<string, FileStat*> Paths;
  Paths paths_;
  FileStat* GetFile(const string& path);
  void Dump();
  void Reload();
};

FileStat* StatCache::GetFile(const string& path) {
  Paths::iterator i = paths_.find(path);
  if (i != paths_.end())
    return i->second;
  FileStat* file = new FileStat(path);
  paths_[path] = file;
  return file;
}

#include <stdio.h>

void StatCache::Dump() {
  for (Paths::iterator i = paths_.begin(); i != paths_.end(); ++i) {
    printf("%s %s\n", i->second->path_.c_str(),
           i->second->node_->dirty_ ? "dirty" : "clean");
  }
}

void StatCache::Reload() {
  set<Edge*> leaf_edges;
  for (Paths::iterator i = paths_.begin(); i != paths_.end(); ++i) {
    bool exists = i->second->Stat();
    Node* node = i->second->node_;
    node->dirty_ = !exists;
    if (!node->in_edge_) {
      for (vector<Edge*>::iterator j = node->out_edges_.begin();
           j != node->out_edges_.end(); ++j) {
        leaf_edges.insert(*j);
      }
    }
  }

  for (set<Edge*>::iterator i = leaf_edges.begin(); i != leaf_edges.end(); ++i) {
    (*i)->RecomputeDirty();
  }
}

struct State : public EvalString::Env {
  StatCache stat_cache_;
  map<string, Rule*> rules_;
  vector<Edge*> edges_;
  map<string, string> env_;

  StatCache* stat_cache() { return &stat_cache_; }

  // EvalString::Env impl
  virtual string Evaluate(const string& var);

  Rule* AddRule(const string& name, const string& command);
  Edge* AddEdge(Rule* rule);
  Edge* AddEdge(const string& rule_name);
  Node* GetNode(const string& path);
  void AddInOut(Edge* edge, Edge::InOut inout, const string& path);
  void AddBinding(const string& key, const string& val);
};

string State::Evaluate(const string& var) {
  if (var.size() > 1 && var[0] == '$') {
    map<string, string>::iterator i = env_.find(var.substr(1));
    if (i != env_.end())
      return i->second;
  }
  return "";
}

Rule* State::AddRule(const string& name, const string& command) {
  Rule* rule = new Rule(name, command);
  rules_[name] = rule;
  return rule;
}

Edge* State::AddEdge(const string& rule_name) {
  return AddEdge(rules_[rule_name]);
}

Edge* State::AddEdge(Rule* rule) {
  Edge* edge = new Edge();
  edge->rule_ = rule;
  edge->env_ = this;
  edges_.push_back(edge);
  return edge;
}

Node* State::GetNode(const string& path) {
  FileStat* file = stat_cache_.GetFile(path);
  if (!file->node_)
    file->node_ = new Node(file);
  return file->node_;
}

void State::AddInOut(Edge* edge, Edge::InOut inout, const string& path) {
  Node* node = GetNode(path);
  if (inout == Edge::IN) {
    edge->inputs_.push_back(node);
    node->out_edges_.push_back(edge);
  } else {
    edge->outputs_.push_back(node);
    assert(node->in_edge_ == NULL);
    node->in_edge_ = edge;
  }
}

void State::AddBinding(const string& key, const string& val) {
  env_[key] = val;
}

struct Plan {
  explicit Plan(State* state) : state_(state) {}

  void AddTarget(const string& path);
  bool AddTarget(Node* node);

  Edge* FindWork();
  void EdgeFinished(Edge* edge);
  void NodeFinished(Node* node);

  State* state_;
  set<Node*> want_;
  queue<Edge*> ready_;

private:
  Plan();
  Plan(const Plan&);
};

void Plan::AddTarget(const string& path) {
  AddTarget(state_->GetNode(path));
}
bool Plan::AddTarget(Node* node) {
  if (!node->dirty())
    return false;
  Edge* edge = node->in_edge_;
  assert(edge);  // Only nodes with in-edges can be dirty.

  want_.insert(node);

  bool awaiting_inputs = false;
  for (vector<Node*>::iterator i = edge->inputs_.begin();
       i != edge->inputs_.end(); ++i) {
    if (AddTarget(*i))
      awaiting_inputs = true;
  }

  if (!awaiting_inputs)
    ready_.push(edge);

  return true;
}

Edge* Plan::FindWork() {
  if (ready_.empty())
    return NULL;
  Edge* edge = ready_.front();
  ready_.pop();
  return edge;
}

void Plan::EdgeFinished(Edge* edge) {
  // Check off any nodes we were waiting for with this edge.
  for (vector<Node*>::iterator i = edge->outputs_.begin();
       i != edge->outputs_.end(); ++i) {
    set<Node*>::iterator j = want_.find(*i);
    if (j != want_.end()) {
      NodeFinished(*j);
      want_.erase(j);
    }
  }
}

void Plan::NodeFinished(Node* node) {
  // See if we we want any edges from this node.
  for (vector<Edge*>::iterator i = node->out_edges_.begin();
       i != node->out_edges_.end(); ++i) {
    // See if we want any outputs from this edge.
    for (vector<Node*>::iterator j = (*i)->outputs_.begin();
         j != (*i)->outputs_.end(); ++j) {
      if (want_.find(*j) != want_.end()) {
        // See if the edge is ready.
        // XXX just track dirty counts.
        // XXX may double-enqueue edge.
        bool ready = true;
        for (vector<Node*>::iterator k = (*i)->inputs_.begin();
             k != (*i)->inputs_.end(); ++k) {
          if ((*k)->dirty()) {
            ready = false;
            break;
          }
        }
        if (ready)
          ready_.push(*i);
        break;
      }
    }
  }
}


#include "manifest_parser.h"

struct Shell {
  virtual ~Shell() {}
  virtual bool RunCommand(Edge* edge);
};

bool Shell::RunCommand(Edge* edge) {
  string err;
  string command = edge->EvaluateCommand();
  printf("  %s\n", command.c_str());
  int ret = system(command.c_str());
  if (WIFEXITED(ret)) {
    int exit = WEXITSTATUS(ret);
    if (exit == 0)
      return true;
    err = "nonzero exit status";
  } else {
    err = "something else went wrong";
  }
  return false;
}

struct Builder {
  Builder(State* state) : plan_(state) {}
  virtual ~Builder() {}

  void AddTarget(const string& name) {
    plan_.AddTarget(name);
  }
  bool Build(Shell* shell, string* err);

  Plan plan_;
};

bool Builder::Build(Shell* shell, string* err) {
  if (plan_.want_.empty()) {
    *err = "no work to do";
    return true;
  }

  Edge* edge = plan_.FindWork();
  if (!edge) {
    *err = "unable to find work";
    return false;
  }

  do {
    string command = edge->EvaluateCommand();
    if (!shell->RunCommand(edge)) {
      err->assign("command '" + command + "' failed.");
      return false;
    }
    for (vector<Node*>::iterator i = edge->outputs_.begin();
         i != edge->outputs_.end(); ++i) {
      // XXX check that the output actually changed
      // XXX just notify node and have it propagate?
      (*i)->dirty_ = false;
    }
    plan_.EdgeFinished(edge);
  } while ((edge = plan_.FindWork()) != NULL);

  if (!plan_.want_.empty()) {
    *err = "ran out of work";
    return false;
  }

  return true;
}