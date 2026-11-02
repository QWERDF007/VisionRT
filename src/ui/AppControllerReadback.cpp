#include "ui/AppController_p.h"

namespace visionrt {

bool AppController::Impl::readsAllowed() const noexcept
{
    return presentation.readsAllowed();
}

void AppController::Impl::drainReaders()
{
    presentation.drainReaders();
}

void AppController::Impl::beginDrain(std::function<void()> continuation)
{
    // A preview must stop touching the data the caller is about to mutate. Only an invalidation
    // belongs here: the persistent helper is released by the management operations that really end
    // its session (reset, catalog reload, close), otherwise every drain would tear it down again.
    if (assist && !presentation.draining())
        assist->invalidate();
    presentation.beginDrain(std::move(continuation));
}

void AppController::Impl::finishDrain()
{
    presentation.finishDrain();
}

void AppController::Impl::updateReadback()
{
    presentation.updateReadback();
}

void AppController::requestOutputText(const QVariantMap &context, bool copy, bool diagnostic)
{
    impl_->presentation.requestText(context, copy, diagnostic);
}

} // namespace visionrt
