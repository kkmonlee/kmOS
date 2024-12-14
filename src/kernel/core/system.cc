#include <os.h>

/*
 * System class manages users, environment variables, and system organization.
 */

System::System() {}

System::~System() {}

void System::init()
{
  var = fsm.path("/sys/env/");

  // system users
  root = new User("root");
  root->setUType(USER_ROOT);

  actual = new User("liveuser");

  // environment variables
  uservar = new Variable("USER", "liveuser");
  new Variable("OS_NAME", KERNEL_NAME);
  new Variable("OS_VERSION", KERNEL_VERSION);
  new Variable("OS_DATE", KERNEL_DATE);
  new Variable("OS_TIME", KERNEL_TIME);
  new Variable("OS_LICENCE", KERNEL_LICENCE);
  new Variable("COMPUTERNAME", KERNEL_COMPUTERNAME);
  new Variable("PROCESSOR_IDENTIFIER", KERNEL_PROCESSOR_IDENTIFIER);
  new Variable("PROCESSOR_NAME", arch.detect());
  new Variable("PATH", "/bin/");
  new Variable("SHELL", "/bin/sh");
}

// login function
int System::login(User *us, char *pass)
{
  if (us == NULL)
    return ERROR_PARAM;

  if (us->getPassword() != NULL)
  {                   // user has a password
    if (pass == NULL) // no password provided
      return PARAM_NULL;
    if (strncmp(pass, us->getPassword(), strlen(us->getPassword())))
      return RETURN_FAILURE; // password mismatch
  }

  uservar->write(0, (u8 *)us->getName(), strlen(us->getName()));
  actual = us;
  return RETURN_OK;
}

// retrieves the value of a variable; caller must free the memory
char *System::getvar(char *name)
{
  File *temp = var->find(name);
  if (temp == NULL)
    return NULL;

  char *varin = (char *)kmalloc(temp->getSize());
  memset(varin, 0, temp->getSize());
  temp->read(0, (u8 *)varin, temp->getSize());
  return varin;
}

// retrieves a user by name
User *System::getUser(char *nae)
{
  User *us = listuser;
  while (us != NULL)
  {
    if (!strncmp(nae, us->getName(), strlen(us->getName())))
      return us;
    us = us->getUNext();
  }
  return NULL;
}

// adds a user to the user list
void System::addUserToList(User *us)
{
  if (us == NULL)
    return;
  us->setUNext(listuser);
  listuser = us;
}

// checks if the current user is root
u32 System::isRoot()
{
  return (actual != NULL && actual->getUType() == USER_ROOT) ? 1 : 0;
}
