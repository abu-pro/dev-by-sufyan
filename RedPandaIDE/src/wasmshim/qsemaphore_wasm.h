/*
 * Dev by Sufyan — WebAssembly port (Part 6).
 *
 * Minimal QSemaphore stand-in for the SINGLE-THREADED WASM build
 * (DBS_WASM_ST). Qt 6.8.3's wasm_singlethread kit does not ship
 * QSemaphore at all (there is no qsemaphore.h in its QtCore — the
 * class needs real threads), while the multithread kit and every
 * native build have it. Only four app headers include <QSemaphore>,
 * all for start/stop synchronisation inside features that are inert
 * in the web build (the native executable runner's quit handshake,
 * the debugger's start handshake, the Competitive Companion
 * listener's start/stop handshake — the debugger is disabled in the
 * web UI and the native runner is never used; see Part 2). This
 * counter-only version lets those sources compile unchanged.
 *
 * Semantics, honestly: release()/tryAcquire() behave like the real
 * counter. acquire() takes the permits when they are available and
 * otherwise returns WITHOUT waiting — a browser main thread must
 * never block waiting for another thread that does not exist. No
 * web-build behaviour depends on the difference: the synchronised
 * features never run here.
 *
 * Only ever included under DBS_WASM_ST, in place of <QSemaphore>.
 */
#ifndef DBS_QSEMAPHORE_WASM_H
#define DBS_QSEMAPHORE_WASM_H

#ifdef DBS_WASM_ST

class QSemaphore
{
public:
    explicit QSemaphore(int n = 0) : mAvailable(n) {}
    QSemaphore(const QSemaphore &) = delete;
    QSemaphore &operator=(const QSemaphore &) = delete;
    ~QSemaphore() = default;

    void acquire(int n = 1)
    {
        if (mAvailable >= n)
            mAvailable -= n;
        // else: cannot block in a single-threaded browser build;
        // degrade silently (see header comment).
    }
    bool tryAcquire(int n = 1)
    {
        if (mAvailable < n)
            return false;
        mAvailable -= n;
        return true;
    }
    bool tryAcquire(int n, int /*timeout*/)
    {
        return tryAcquire(n);
    }
    void release(int n = 1) { mAvailable += n; }
    int available() const { return mAvailable; }

private:
    int mAvailable;
};

#endif // DBS_WASM_ST

#endif // DBS_QSEMAPHORE_WASM_H
