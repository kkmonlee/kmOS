#ifndef SYSTEM_H
#define SYSTEM_H

#include <runtime/types.h>
#include <core/env.h>
#include <core/user.h>

class System
{
public:
  System();
  ~System();

  void init();
  char *getvar(char *name);

  void addUserToList(User *us);
  User *getUser(char *name);

  int login(User *us, char *pass);
  u32 isRoot(); // returns 1 if root user

private:
  User *listuser;    // list of users
  File *var;         // file for system variables
  User *actual;      // currently logged-in user
  User *root;        // root user
  Variable *uservar; // variable for user-specific data
};

extern System sys;

#endif
