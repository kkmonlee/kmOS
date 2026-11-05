#include <os.h>
#include <api.h>
#include <architecture.h>
#include <process.h>
#include <vmm.h>

struct Pipe {
    File *reader = nullptr;
    File *writer = nullptr;
    u32 first = 0;
    u32 size = 0;
    u8 data[PAGESIZE];
};

class PipeEnd : public File {
public:
    PipeEnd(Pipe *pipe, bool reader) : File(nullptr, TYPE_PIPE), pipe_(pipe), reader_(reader) {
        if (reader_) pipe_->reader = this;
        else pipe_->writer = this;
    }

    ~PipeEnd() override {
        if (reader_) {
            pipe_->reader = nullptr;
            if (pipe_->writer) Process::wakeFile(pipe_->writer);
        } else {
            pipe_->writer = nullptr;
            if (pipe_->reader) Process::wakeFile(pipe_->reader);
        }
        if (!pipe_->reader && !pipe_->writer) delete pipe_;
    }

    u32 open(u32) override { return 0; }
    u32 close() override { return 0; }
    bool waitable() const override { return true; }

    u32 read(u32, u8 *buffer, u32 count) override {
        if (!reader_) return (u32)-9;
        if (!pipe_->size) return pipe_->writer ? (u32)-11 : 0;
        if (count > pipe_->size) count = pipe_->size;
        for (u32 i = 0; i < count; ++i) buffer[i] = pipe_->data[(pipe_->first + i) % PAGESIZE];
        pipe_->first = (pipe_->first + count) % PAGESIZE;
        pipe_->size -= count;
        if (pipe_->writer) Process::wakeFile(pipe_->writer);
        return count;
    }

    u32 write(u32, u8 *buffer, u32 count) override {
        if (reader_) return (u32)-9;
        if (!pipe_->reader) return (u32)-32;
        if (count > PAGESIZE - pipe_->size) return (u32)-11;
        u32 end = pipe_->first + pipe_->size;
        for (u32 i = 0; i < count; ++i) pipe_->data[(end + i) % PAGESIZE] = buffer[i];
        pipe_->size += count;
        Process::wakeFile(pipe_->reader);
        return count;
    }

private:
    Pipe *pipe_;
    bool reader_;
};

void call_pipe() {
    Process *process = arch.pcurrent;
    page_directory *pd = process->getPInfo()->pd;
    u32 address = arch.getArg(0);
    int fds[2];
    int error;
    if (!vmm.prepare_user_write(pd, address, sizeof(fds), &error)) {
        if (error == -14) process->exit(142, 14);
        else arch.setRet(error);
        return;
    }
    Pipe *pipe = new Pipe();
    if (!pipe) { arch.setRet((u32)-12); return; }
    PipeEnd *reader = new PipeEnd(pipe, true);
    if (!reader) { delete pipe; arch.setRet((u32)-12); return; }
    PipeEnd *writer = new PipeEnd(pipe, false);
    if (!writer) { delete reader; arch.setRet((u32)-12); return; }

    fds[0] = process->addFile(reader, 0);
    fds[1] = fds[0] < 0 ? fds[0] : (int)process->addFile(writer, 1);
    reader->remove();
    writer->remove();
    if (fds[1] < 0) {
        if (fds[0] >= 0) process->deleteFile(fds[0]);
        arch.setRet(fds[1]);
        return;
    }
    if (!vmm.copy_to_user(pd, address, fds, sizeof(fds), &error)) {
        process->deleteFile(fds[0]);
        process->deleteFile(fds[1]);
        arch.setRet(error);
        return;
    }
    arch.setRet(0);
}
