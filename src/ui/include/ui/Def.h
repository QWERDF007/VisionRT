#pragma once

#include <QObject>

#define VISIONRT_Q_PROPERTY_AUTO(TYPE, M)                    \
    Q_PROPERTY(TYPE M READ M WRITE set##M NOTIFY M##Changed) \
public:                                                      \
    Q_SIGNAL void M##Changed();                              \
                                                             \
    void set##M(const TYPE &in_##M)                          \
    {                                                        \
        if (_##M == in_##M) {                                \
            return;                                          \
        }                                                    \
        _##M = in_##M;                                       \
        Q_EMIT M##Changed();                                 \
    }                                                        \
                                                             \
    void M(const TYPE &in_##M)                               \
    {                                                        \
        set##M(in_##M);                                      \
    }                                                        \
                                                             \
    TYPE M() const                                           \
    {                                                        \
        return _##M;                                         \
    }                                                        \
                                                             \
private:                                                     \
    TYPE _##M {};
