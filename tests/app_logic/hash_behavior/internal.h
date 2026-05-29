#pragma once

#include <QtTest/QtTest>

#include <QDir>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>

#include "archive_error.h"
#include "archive_session.h"

namespace hash_behavior_internal {

namespace fs = std::filesystem;

std::string to_std_path(const QString& path);
std::filesystem::path recycle_bin_dir_for_test(std::error_code& ec);
bool wait_until(const std::function<bool()>& predicate, int timeout_ms);
bool wait_until_stable(const std::atomic<uint64_t>& value,
                       int stable_ms,
                       int timeout_ms);
std::optional<size_t> first_stage_index(
    const std::vector<z7::app::OperationEvent>& events,
    z7::app::OperationStage stage);

bool is_terminal_state(z7::app::ArchiveSessionState state);

class OperationEventCollector final : public z7::app::IArchiveDelegate {
 public:
  explicit OperationEventCollector(std::vector<z7::app::OperationEvent>* sink);

  void on_lifecycle(z7::app::OperationStage stage, std::string_view message) override;
  void on_log(const z7::app::ArchiveLog& log) override;
  void on_progress(const z7::app::ProgressSnapshot& progress) override;

 private:
  void push_event(const z7::app::OperationEvent& event);

  std::vector<z7::app::OperationEvent>* sink_ = nullptr;
  std::mutex mutex_;
};

class WaitableOutcomeDelegate final : public z7::app::IArchiveDelegate {
 public:
  struct DelegateInteraction {
    std::function<z7::app::OverwriteDecision(const z7::app::OverwritePrompt&)>
        request_overwrite;
    std::function<z7::app::PasswordReply(const z7::app::PasswordPrompt&)>
        request_password;
    std::function<z7::app::ChoiceReply(const z7::app::ChoicePrompt&)> request_choice;
    std::function<z7::app::MemoryLimitReply(const z7::app::MemoryLimitPrompt&)>
        request_memory_limit;
  };

  explicit WaitableOutcomeDelegate(
      std::shared_ptr<z7::app::IArchiveDelegate> forward,
      DelegateInteraction interaction = {})
      : forward_(std::move(forward)),
        interaction_(std::move(interaction)) {}

  void on_lifecycle(z7::app::OperationStage stage, std::string_view message) override {
    if (forward_) {
      forward_->on_lifecycle(stage, message);
    }
  }

  void on_log(const z7::app::ArchiveLog& log) override {
    if (forward_) {
      forward_->on_log(log);
    }
  }

  void on_progress(const z7::app::ProgressSnapshot& progress) override {
    if (forward_) {
      forward_->on_progress(progress);
    }
  }

  bool on_list_entries_batch(std::vector<z7::app::ArchiveListEntry>&& batch) override {
    if (forward_) {
      return forward_->on_list_entries_batch(std::move(batch));
    }
    return true;
  }

  void on_finished(const z7::app::OperationOutcome& outcome) override {
    if (forward_) {
      forward_->on_finished(outcome);
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      outcome_ = outcome;
      done_ = true;
    }
    cv_.notify_all();
  }

  std::optional<z7::app::OverwriteDecision> request_overwrite(
      const z7::app::OverwritePrompt& prompt) override {
    if (forward_) {
      const std::optional<z7::app::OverwriteDecision> forwarded =
          forward_->request_overwrite(prompt);
      if (forwarded.has_value()) {
        return forwarded;
      }
    }
    if (interaction_.request_overwrite) {
      return interaction_.request_overwrite(prompt);
    }
    return std::nullopt;
  }

  std::optional<z7::app::PasswordReply> request_password(
      const z7::app::PasswordPrompt& prompt) override {
    if (forward_) {
      const std::optional<z7::app::PasswordReply> forwarded =
          forward_->request_password(prompt);
      if (forwarded.has_value()) {
        return forwarded;
      }
    }
    if (interaction_.request_password) {
      return interaction_.request_password(prompt);
    }
    return std::nullopt;
  }

  std::optional<z7::app::ChoiceReply> request_choice(
      const z7::app::ChoicePrompt& prompt) override {
    if (forward_) {
      const std::optional<z7::app::ChoiceReply> forwarded =
          forward_->request_choice(prompt);
      if (forwarded.has_value()) {
        return forwarded;
      }
    }
    if (interaction_.request_choice) {
      return interaction_.request_choice(prompt);
    }
    return std::nullopt;
  }

  std::optional<z7::app::MemoryLimitReply> request_memory_limit(
      const z7::app::MemoryLimitPrompt& prompt) override {
    if (forward_) {
      const std::optional<z7::app::MemoryLimitReply> forwarded =
          forward_->request_memory_limit(prompt);
      if (forwarded.has_value()) {
        return forwarded;
      }
    }
    if (interaction_.request_memory_limit) {
      return interaction_.request_memory_limit(prompt);
    }
    return std::nullopt;
  }

  z7::app::OperationOutcome await_outcome() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]() { return done_; });
    return outcome_.value_or(z7::app::OperationOutcome{});
  }

 private:
  std::shared_ptr<z7::app::IArchiveDelegate> forward_;
  DelegateInteraction interaction_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::optional<z7::app::OperationOutcome> outcome_;
  bool done_ = false;
};

using DelegateInteraction = WaitableOutcomeDelegate::DelegateInteraction;

struct StartedSession {
  z7::app::ArchiveSession session;
  std::shared_ptr<WaitableOutcomeDelegate> completion_delegate;

  bool valid() const { return session.valid(); }
  void cancel() const { session.cancel(); }
  void pause() const { session.pause(); }
  void resume() const { session.resume(); }
  z7::app::ArchiveSessionState state() const { return session.state(); }
  z7::app::ProgressSnapshot snapshot() const { return session.snapshot(); }
};

template <typename TRequest>
struct RequestResultTraits;

template <>
struct RequestResultTraits<z7::app::AddRequest> {
  using type = z7::app::AddResult;
};
template <>
struct RequestResultTraits<z7::app::ExtractRequest> {
  using type = z7::app::ExtractResult;
};
template <>
struct RequestResultTraits<z7::app::TestRequest> {
  using type = z7::app::TestResult;
};
template <>
struct RequestResultTraits<z7::app::BenchmarkRequest> {
  using type = z7::app::BenchmarkResult;
};
template <>
struct RequestResultTraits<z7::app::SplitRequest> {
  using type = z7::app::SplitResult;
};
template <>
struct RequestResultTraits<z7::app::CombineRequest> {
  using type = z7::app::CombineResult;
};
template <>
struct RequestResultTraits<z7::app::HashRequest> {
  using type = z7::app::HashResult;
};
template <>
struct RequestResultTraits<z7::app::DeleteRequest> {
  using type = z7::app::DeleteResult;
};
template <>
struct RequestResultTraits<z7::app::OpenArchiveRequest> {
  using type = z7::app::OpenArchiveResult;
};
template <>
struct RequestResultTraits<z7::app::ListRequest> {
  using type = z7::app::ListResult;
};
template <>
struct RequestResultTraits<z7::app::ArchivePropertiesRequest> {
  using type = z7::app::ArchivePropertiesResult;
};
template <>
struct RequestResultTraits<z7::app::NavigateRequest> {
  using type = z7::app::NavigateResult;
};
template <>
struct RequestResultTraits<z7::app::CopyRequest> {
  using type = z7::app::CopyResult;
};
template <>
struct RequestResultTraits<z7::app::MoveRequest> {
  using type = z7::app::MoveResult;
};
template <>
struct RequestResultTraits<z7::app::RenameRequest> {
  using type = z7::app::RenameResult;
};
template <>
struct RequestResultTraits<z7::app::CreateRequest> {
  using type = z7::app::CreateResult;
};
template <>
struct RequestResultTraits<z7::app::ArchiveCommentRequest> {
  using type = z7::app::ArchiveCommentResult;
};
template <>
struct RequestResultTraits<z7::app::FilesystemCommentRequest> {
  using type = z7::app::FilesystemCommentResult;
};
template <>
struct RequestResultTraits<z7::app::GetEntryInfoRequest> {
  using type = z7::app::GetEntryInfoResult;
};
template <>
struct RequestResultTraits<z7::app::OpenArchiveFromPathRequest> {
  using type = z7::app::OpenArchiveSessionResult;
};
template <>
struct RequestResultTraits<z7::app::OpenArchiveFromParentRequest> {
  using type = z7::app::OpenArchiveSessionResult;
};
template <>
struct RequestResultTraits<z7::app::CloseArchiveSessionRequest> {
  using type = z7::app::OperationResult;
};

template <typename TRequest>
using RequestResultT = typename RequestResultTraits<std::decay_t<TRequest>>::type;

template <typename TResult>
TResult result_from_outcome(const z7::app::OperationOutcome& outcome) {
  if constexpr (!std::is_same_v<TResult, z7::app::OperationResult>) {
    if (const std::optional<TResult> typed = z7::app::outcome_payload_as<TResult>(outcome);
        typed.has_value()) {
      return *typed;
    }
  }
  TResult fallback;
  fallback.ok = outcome.ok;
  fallback.error = outcome.error;
  fallback.native_exit_code = outcome.native_code;
  fallback.native_execution = outcome.native_execution;
  fallback.summary = outcome.summary;
  return fallback;
}

template <typename TRequest>
RequestResultT<TRequest> run_request_sync_impl(
    const TRequest& request,
    std::vector<z7::app::OperationEvent>* events,
    const DelegateInteraction& interaction) {
  DelegateInteraction effective_interaction = interaction;
  if (!effective_interaction.request_password) {
    effective_interaction.request_password =
        [](const z7::app::PasswordPrompt&) {
          z7::app::PasswordReply reply;
          reply.kind = z7::app::PasswordReplyKind::kProvide;
          reply.password = "test-password";
          return reply;
        };
  }

  std::shared_ptr<OperationEventCollector> delegate;
  if (events != nullptr) {
    delegate = std::make_shared<OperationEventCollector>(events);
  }
  auto completion_delegate = std::make_shared<WaitableOutcomeDelegate>(
      std::move(delegate),
      std::move(effective_interaction));

  z7::app::ArchiveRequest archive_request;
  archive_request.payload = request;
  z7::app::ArchiveEngine engine;
  z7::app::ArchiveSession session = engine.start(archive_request, completion_delegate);
  z7::app::OperationOutcome outcome;
  if (session.valid()) {
    outcome = completion_delegate->await_outcome();
  } else {
    outcome.status = z7::app::OperationStatus::kFailed;
    outcome.error_domain = z7::app::ArchiveErrorDomain::kBackendUnavailable;
    outcome.native_code = 2;
    outcome.ok = false;
    outcome.error = z7::app::make_archive_error(
        z7::app::ArchiveErrorDomain::kBackendUnavailable,
        "No archive backend available.",
        outcome.native_code);
    outcome.native_execution.native_exit_code = outcome.native_code;
    outcome.native_execution.termination_reason = z7::app::NativeTerminationReason::kCompleted;
    outcome.summary = z7::app::describe_archive_error(outcome.error);
  }
  return result_from_outcome<RequestResultT<TRequest>>(outcome);
}

template <typename TRequest>
RequestResultT<TRequest> run_request_sync(const TRequest& request) {
  return run_request_sync_impl(request, nullptr, {});
}

template <typename TRequest>
RequestResultT<TRequest> run_request_sync(
    const TRequest& request,
    std::vector<z7::app::OperationEvent>& events) {
  return run_request_sync_impl(request, &events, {});
}

template <typename TRequest>
RequestResultT<TRequest> run_request_sync(
    const TRequest& request,
    const DelegateInteraction& interaction) {
  return run_request_sync_impl(request, nullptr, interaction);
}

template <typename TRequest>
RequestResultT<TRequest> run_request_sync(
    const TRequest& request,
    std::vector<z7::app::OperationEvent>& events,
    const DelegateInteraction& interaction) {
  return run_request_sync_impl(request, &events, interaction);
}

template <typename TRequest>
StartedSession start_request(
    const TRequest& request,
    std::shared_ptr<z7::app::IArchiveDelegate> delegate = {},
    const DelegateInteraction& interaction = {}) {
  DelegateInteraction effective_interaction = interaction;
  if (!effective_interaction.request_password) {
    effective_interaction.request_password =
        [](const z7::app::PasswordPrompt&) {
          z7::app::PasswordReply reply;
          reply.kind = z7::app::PasswordReplyKind::kProvide;
          reply.password = "test-password";
          return reply;
        };
  }

  z7::app::ArchiveRequest archive_request;
  archive_request.payload = request;
  z7::app::ArchiveEngine engine;
  auto completion_delegate =
      std::make_shared<WaitableOutcomeDelegate>(std::move(delegate),
                                                std::move(effective_interaction));
  StartedSession started;
  started.completion_delegate = completion_delegate;
  started.session = engine.start(archive_request, completion_delegate);
  return started;
}

template <typename TResult>
TResult wait_for_result(const StartedSession& task) {
  z7::app::OperationOutcome outcome;
  if (!task.valid() || !task.completion_delegate) {
    outcome.status = z7::app::OperationStatus::kFailed;
    outcome.error_domain = z7::app::ArchiveErrorDomain::kBackendUnavailable;
    outcome.native_code = 2;
    outcome.ok = false;
    outcome.error = z7::app::make_archive_error(
        z7::app::ArchiveErrorDomain::kBackendUnavailable,
        "No archive backend available.",
        outcome.native_code);
    outcome.native_execution.native_exit_code = outcome.native_code;
    outcome.native_execution.termination_reason = z7::app::NativeTerminationReason::kCompleted;
    outcome.summary = z7::app::describe_archive_error(outcome.error);
    return result_from_outcome<TResult>(outcome);
  }
  return result_from_outcome<TResult>(task.completion_delegate->await_outcome());
}

}  // namespace hash_behavior_internal

class AppLogicHashBehaviorTest final : public QObject {
  Q_OBJECT

 private slots:
  void singleMethodAndAllMethodsProduceDigestSummary();
  void hashRequestRejectsUnknownMethod();
  void hashRequestReturnsPartialSuccessWhenInputMissing();
  void directoryRecursionFlagControlsSummaryCounts();
  void hashPauseResumeCancelStateMachine();
  void openListPropertiesNavigateRequestsCoverSuccessAndFailure();
  void listReturnsTypedEntriesFromOriginalApi();
  void addRequestZipPasswordWithDisabledEncryptedHeadersSucceeds();
  void addRequest7zEncryptedHeadersRequirePasswordOnOpen();
  void listUsesFileManagerRootViewSemantics();
  void listSupportsVirtualDirectoryTraversal();
  void listCancelStateMachineWithHugeDirectoryFilter();
  void addRequestDirectoryTargetsArchiveVirtualDirectory();
  void listDetailedPropsIncludeArchivePreviewFields();
  void archivePropertiesReturnStructuredLines();
  void openListPropertiesCancelStateMachine();
  void testArchiveUsesOriginalApiPathAndStructuredProgress();
  void testArchivePathsContinueAfterInvalidInputLikeOriginal();
  void testArchivePathsExpandDirectoriesLikeOriginal();
  void extractArchiveUsesOriginalApiPathAndStructuredProgress();
  void testArchivePauseResumeStateMachine();
  void releasingRunningSessionDoesNotSelfJoinWorkerThread();
  void deleteArchiveUsesOriginalApiPathAndStructuredProgress();
  void deleteFilesystemPathsMovesItemsToRecycleBin();
  void deleteFilesystemPathsCanBypassRecycleBinWhenRequested();
  void copyMoveSupportSingleSourceDestinationPathRename();
  void benchmarkRequestReturnsSummaryLine();
  void backendCapabilitiesExposeSplitCombineAndTypedBenchmark();
  void benchmarkTypedSnapshotsExposeStructuredSizes();
  void benchmarkMemoryLimitRequiresTypedHandler();
  void benchmarkMemoryLimitExplicitHandlerTakesPriorityOverChoice();
  void benchmarkMemoryLimitUpdatedLimitBelowEstimateCancelsOperation();
  void splitAndCombineRoundTripMatchesSourceBytes();
  void splitAndCombineReportExpectedErrors();
  void extractTestCopyMoveDeleteRequestsCoverExtendedModels();
  void nestedOpenChoosesExpectedStrategiesForEmbeddedZipArchives();
  void nestedOpenSupportsSameNameEmbeddedArchiveEntries();
  void nestedArchiveSessionsSupportParentCloseBeforeChildClose();
  void nestedDeleteWritebackRemovesEmbeddedArchiveEntry();
  void nestedUpdateWritebackReplacesEmbeddedArchiveContent();
  void nestedDeleteWritebackSupportsEmbeddedArchivePathsWithDirectories();
  void addRequestInputItemsTargetExplicitArchiveEntriesAndMergeOverlaps();
  void addRequestUpdateModeMatrixMatchesOriginalActionSets();
  void addRequestPathModeMatrixMatchesOriginalCensorModes();
  void addRequestUpdateModeAppliesOriginalActionSetSemantics();
  void addRequestRawUpdateSwitchOverridesModeAndSkipsOnlyDiskItems();
  void addRequestRawUSwitchCreatesAdditionalArchiveTarget();
  void addRequestRawUSwitchCreatesMultipleAdditionalArchiveTargets();
  void addRequestRawUSwitchesIgnoreMainAndCreateAdditionalArchive();
  void addRequestRawUSwitchIgnoreMainArchiveLeavesContentUnchanged();
  void addRequestInvalidRawUpdateSwitchReturnsInvalidArguments();
  void addRequestInputItemsRejectInvalidContractsAndMissingSources();
  void addRequestReturnsPartialSuccessWhenInputMissing();
  void extractAskOverwriteCallbackDecisionsAreApplied();
  void extractAskOverwriteStickyNoToAllSkipsRemainingConflicts();
  void extractAskOverwriteWithoutCallbackReportsMissingInteraction();
  void copyMoveAskOverwriteCallbackDecisionsAreApplied();
  void copyMoveAskOverwriteStickyAllDecisionsApplyToRemainingConflicts();
  void commentRequestsCoverArchiveAndFilesystemModels();
  void copyMoveDeleteInvalidRequestsAndCancelPathsMapCorrectDomains();
  void progressLifecycleEventsAreTypedAndOrdered();
  void createAndRenameRequestsReportInvalidArguments();
  void createFileRequestDoesNotOverwriteExistingFile();
  void benchmarkRequestRejectsInvalidDictionarySize();
  void testArchiveCancelStateMachine();
  void splitAndCombineCancelStateMachine();
  void errorDomainMappingIgnoresDiagnosticKeywords();
  void operationOutcomeMatrixIsCompleteAndMapped();
  // B1: ExtractResult materialized entries
  void extractMaterializedEntriesSingleFile();
  void extractMaterializedEntriesPrimaryPath();
  void extractMaterializedEntriesRenameCollision();
  void extractMaterializedEntriesSkipNotRecorded();
  void extractMaterializedEntriesMultiArchiveMerged();
  void extractMaterializedEntriesOverwriteFlag();
  void extractMaterializedEntriesOverwriteLeavesNoTempResidue();
  void extractMaterializedEntriesPathRemapsDirectWrite();
  void extractMaterializedEntriesRollbackRestoresOverwrittenFile();
  void extractMaterializedEntriesWrongPasswordNewFileLeavesNoResidue();
  void extractMaterializedEntriesWrongPasswordExistingFileRestoresOriginal();
  void extractMaterializedEntriesRenameExistingSuccessKeepsReadableBackup();
  void extractMaterializedEntriesRenameExistingFailureRestoresOriginal();
  void extractOutputTemplateNamesFollowArchiveSuffixRules();
  void extractOutputTailNameNormalizesSeparatorsAndTrailingSlashes();
  // B3: ExtractBudget
  void extractBudgetMaxFilesRollback();
  void extractBudgetRollbackRestoresOverwrittenFile();
  void extractBudgetMaxFilesKeepPartial();
  void extractBudgetMaxFilesTruncate();
  void extractBudgetExactLimitNoTrigger();
  void extractBudgetNulloptNoBehaviourChange();
  // B2: GetEntryInfo
  void getEntryInfoDirectOpenSingleFile();
  void getEntryInfoDirectoryReturnsSubtreeStats();
  void getEntryInfoNotFoundReturnsFalse();
  void getEntryInfoRootPathIsDirectory();
  void getEntryInfoSessionReuseNoReopen();
  // B4: ListRequest streaming batch callback
  void listStreamingModeOffPreservesOldBehaviour();
  void listStreamingModeBatchesDeliveredAndEntriesEmpty();
  void listStreamingModeDelegateEarlyStopCancels();
  void listStreamingModeSessionTokenReuse();
  void atomicReplaceHelperRestoresOriginalAndPreservesTempOnPromoteFailure();
  void atomicReplaceHelperFailsWhenBackupPathProbeFails();
};
