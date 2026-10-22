#pragma once
#include <QLockFile>
// One credential-owning application process per Windows user/profile.
class InstanceGuard {
    QLockFile lock;

  public:
    explicit InstanceGuard(QString file) : lock(file) {
        lock.setStaleLockTime(0);
    }
    bool acquire() {
        return lock.tryLock(0);
    }
};
