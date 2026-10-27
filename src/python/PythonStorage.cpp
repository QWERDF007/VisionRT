#include "python/PythonStorage.h"

#include <condition_variable>
#include <mutex>
#include <optional>

namespace visionrt {
namespace py = pybind11;

struct PythonStorage::State
{
    std::mutex              mutex;
    std::condition_variable released;
    std::size_t             active = 0;

    struct Reference
    {
        std::shared_ptr<State> state;
        PyObject              *object;

        Reference(std::shared_ptr<State> owner, PyObject *value)
            : state(std::move(owner))
            , object(value)
        {
            Py_INCREF(object);
            std::lock_guard lock(state->mutex);
            ++state->active;
        }

        ~Reference()
        {
            {
                py::gil_scoped_acquire gil;
                Py_DECREF(object);
            }
            {
                std::lock_guard lock(state->mutex);
                --state->active;
            }
            state->released.notify_all();
        }
    };
};

PythonStorage::PythonStorage()
    : state_(std::make_shared<State>())
{
}

Owner PythonStorage::retain(py::handle object) const
{
    return std::make_shared<State::Reference>(state_, object.ptr());
}

void PythonStorage::waitForRelease() const
{
    std::optional<py::gil_scoped_release> gil;
    if (PyGILState_Check())
        gil.emplace();
    std::unique_lock lock(state_->mutex);
    state_->released.wait(lock, [&] { return state_->active == 0; });
}

std::size_t PythonStorage::activeCount() const noexcept
{
    std::lock_guard lock(state_->mutex);
    return state_->active;
}

bool PythonStorage::isReadyForClose() const noexcept
{
    std::lock_guard lock(state_->mutex);
    return state_->active == 0;
}

} // namespace visionrt
