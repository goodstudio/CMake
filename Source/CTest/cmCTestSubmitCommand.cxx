/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#include "cmCTestSubmitCommand.h"

#include <set>
#include <sstream>
#include <utility>

#include <cm/memory>
#include <cm/vector>
#include <cmext/string_view>

#include "cmCTest.h"
#include "cmCTestGenericHandler.h"
#include "cmCTestSubmitHandler.h"
#include "cmCommand.h"
#include "cmList.h"
#include "cmMakefile.h"
#include "cmMessageType.h"
#include "cmRange.h"
#include "cmSystemTools.h"
#include "cmValue.h"

class cmExecutionStatus;

/**
 * This is a virtual constructor for the command.
 */
std::unique_ptr<cmCommand> cmCTestSubmitCommand::Clone()
{
  auto ni = cm::make_unique<cmCTestSubmitCommand>();
  ni->CTest = this->CTest;
  return std::unique_ptr<cmCommand>(std::move(ni));
}

std::unique_ptr<cmCTestGenericHandler>
cmCTestSubmitCommand::InitializeHandler()
{
  cmValue submitURL = !this->SubmitURL.empty()
    ? cmValue(this->SubmitURL)
    : this->Makefile->GetDefinition("CTEST_SUBMIT_URL");

  if (submitURL) {
    this->CTest->SetCTestConfiguration("SubmitURL", *submitURL, this->Quiet);
  } else {
    this->CTest->SetCTestConfigurationFromCMakeVariable(
      this->Makefile, "DropMethod", "CTEST_DROP_METHOD", this->Quiet);
    this->CTest->SetCTestConfigurationFromCMakeVariable(
      this->Makefile, "DropSiteUser", "CTEST_DROP_SITE_USER", this->Quiet);
    this->CTest->SetCTestConfigurationFromCMakeVariable(
      this->Makefile, "DropSitePassword", "CTEST_DROP_SITE_PASSWORD",
      this->Quiet);
    this->CTest->SetCTestConfigurationFromCMakeVariable(
      this->Makefile, "DropSite", "CTEST_DROP_SITE", this->Quiet);
    this->CTest->SetCTestConfigurationFromCMakeVariable(
      this->Makefile, "DropLocation", "CTEST_DROP_LOCATION", this->Quiet);
  }

  if (!this->CTest->SetCTestConfigurationFromCMakeVariable(
        this->Makefile, "TLSVersion", "CTEST_TLS_VERSION", this->Quiet)) {
    if (cmValue tlsVersionVar =
          this->Makefile->GetDefinition("CMAKE_TLS_VERSION")) {
      cmCTestOptionalLog(
        this->CTest, HANDLER_VERBOSE_OUTPUT,
        "SetCTestConfiguration from CMAKE_TLS_VERSION:TLSVersion:"
          << *tlsVersionVar << std::endl,
        this->Quiet);
      this->CTest->SetCTestConfiguration("TLSVersion", *tlsVersionVar,
                                         this->Quiet);
    } else if (cm::optional<std::string> tlsVersionEnv =
                 cmSystemTools::GetEnvVar("CMAKE_TLS_VERSION")) {
      cmCTestOptionalLog(
        this->CTest, HANDLER_VERBOSE_OUTPUT,
        "SetCTestConfiguration from ENV{CMAKE_TLS_VERSION}:TLSVersion:"
          << *tlsVersionEnv << std::endl,
        this->Quiet);
      this->CTest->SetCTestConfiguration("TLSVersion", *tlsVersionEnv,
                                         this->Quiet);
    }
  }
  if (!this->CTest->SetCTestConfigurationFromCMakeVariable(
        this->Makefile, "TLSVerify", "CTEST_TLS_VERIFY", this->Quiet)) {
    if (cmValue tlsVerifyVar =
          this->Makefile->GetDefinition("CMAKE_TLS_VERIFY")) {
      cmCTestOptionalLog(
        this->CTest, HANDLER_VERBOSE_OUTPUT,
        "SetCTestConfiguration from CMAKE_TLS_VERIFY:TLSVerify:"
          << *tlsVerifyVar << std::endl,
        this->Quiet);
      this->CTest->SetCTestConfiguration("TLSVerify", *tlsVerifyVar,
                                         this->Quiet);
    } else if (cm::optional<std::string> tlsVerifyEnv =
                 cmSystemTools::GetEnvVar("CMAKE_TLS_VERIFY")) {
      cmCTestOptionalLog(
        this->CTest, HANDLER_VERBOSE_OUTPUT,
        "SetCTestConfiguration from ENV{CMAKE_TLS_VERIFY}:TLSVerify:"
          << *tlsVerifyEnv << std::endl,
        this->Quiet);
      this->CTest->SetCTestConfiguration("TLSVerify", *tlsVerifyEnv,
                                         this->Quiet);
    }
  }
  this->CTest->SetCTestConfigurationFromCMakeVariable(
    this->Makefile, "CurlOptions", "CTEST_CURL_OPTIONS", this->Quiet);
  this->CTest->SetCTestConfigurationFromCMakeVariable(
    this->Makefile, "SubmitInactivityTimeout",
    "CTEST_SUBMIT_INACTIVITY_TIMEOUT", this->Quiet);

  cmValue notesFilesVariable =
    this->Makefile->GetDefinition("CTEST_NOTES_FILES");
  if (notesFilesVariable) {
    cmList notesFiles{ *notesFilesVariable };
    this->CTest->GenerateNotesFile(this->Makefile->GetCMakeInstance(),
                                   notesFiles);
  }

  cmValue extraFilesVariable =
    this->Makefile->GetDefinition("CTEST_EXTRA_SUBMIT_FILES");
  if (extraFilesVariable) {
    cmList extraFiles{ *extraFilesVariable };
    if (!this->CTest->SubmitExtraFiles(extraFiles)) {
      this->SetError("problem submitting extra files.");
      return nullptr;
    }
  }

  auto handler = cm::make_unique<cmCTestSubmitHandler>(this->CTest);

  // If no FILES or PARTS given, *all* PARTS are submitted by default.
  //
  // If FILES are given, but not PARTS, only the FILES are submitted
  // and *no* PARTS are submitted.
  //  (This is why we select the empty "noParts" set in the
  //   if(this->Files) block below...)
  //
  // If PARTS are given, only the selected PARTS are submitted.
  //
  // If both PARTS and FILES are given, only the selected PARTS *and*
  // all the given FILES are submitted.

  // If given explicit FILES to submit, pass them to the handler.
  //
  if (this->Files) {
    // Intentionally select *no* PARTS. (Pass an empty set.) If PARTS
    // were also explicitly mentioned, they will be selected below...
    // But FILES with no PARTS mentioned should just submit the FILES
    // without any of the default parts.
    //
    handler->SelectParts(std::set<cmCTest::Part>());
    handler->SelectFiles(
      std::set<std::string>(this->Files->begin(), this->Files->end()));
  }

  // If a PARTS option was given, select only the named parts for submission.
  //
  if (this->Parts) {
    auto parts =
      cmMakeRange(*(this->Parts)).transform([this](std::string const& arg) {
        return this->CTest->GetPartFromName(arg);
      });
    handler->SelectParts(std::set<cmCTest::Part>(parts.begin(), parts.end()));
  }

  // Pass along any HTTPHEADER to the handler if this option was given.
  if (!this->HttpHeaders.empty()) {
    handler->SetHttpHeaders(this->HttpHeaders);
  }

  handler->RetryDelay = this->RetryDelay;
  handler->RetryCount = this->RetryCount;
  handler->InternalTest = this->InternalTest;

  handler->SetQuiet(this->Quiet);

  if (this->CDashUpload) {
    handler->CDashUpload = true;
    handler->CDashUploadFile = this->CDashUploadFile;
    handler->CDashUploadType = this->CDashUploadType;
  }
  return std::unique_ptr<cmCTestGenericHandler>(std::move(handler));
}

bool cmCTestSubmitCommand::InitialPass(std::vector<std::string> const& args,
                                       cmExecutionStatus& status)
{
  this->CDashUpload = !args.empty() && args[0] == "CDASH_UPLOAD";

  bool ret = this->cmCTestHandlerCommand::InitialPass(args, status);

  if (!this->BuildID.empty()) {
    this->Makefile->AddDefinition(this->BuildID, this->CTest->GetBuildID());
  }

  return ret;
}

void cmCTestSubmitCommand::BindArguments()
{
  if (this->CDashUpload) {
    // Arguments specific to the CDASH_UPLOAD signature.
    this->Bind("CDASH_UPLOAD", this->CDashUploadFile);
    this->Bind("CDASH_UPLOAD_TYPE", this->CDashUploadType);
  } else {
    // Arguments that cannot be used with CDASH_UPLOAD.
    this->Bind("PARTS"_s, this->Parts);
    this->Bind("FILES"_s, this->Files);
  }
  // Arguments used by both modes.
  this->Bind("BUILD_ID"_s, this->BuildID);
  this->Bind("HTTPHEADER"_s, this->HttpHeaders);
  this->Bind("RETRY_COUNT"_s, this->RetryCount);
  this->Bind("RETRY_DELAY"_s, this->RetryDelay);
  this->Bind("SUBMIT_URL"_s, this->SubmitURL);
  this->Bind("INTERNAL_TEST_CHECKSUM", this->InternalTest);

  // Look for other arguments.
  this->cmCTestHandlerCommand::BindArguments();
}

void cmCTestSubmitCommand::CheckArguments()
{
  if (this->Parts) {
    cm::erase_if(*(this->Parts), [this](std::string const& arg) -> bool {
      cmCTest::Part p = this->CTest->GetPartFromName(arg);
      if (p == cmCTest::PartCount) {
        std::ostringstream e;
        e << "Part name \"" << arg << "\" is invalid.";
        this->Makefile->IssueMessage(MessageType::FATAL_ERROR, e.str());
        return true;
      }
      return false;
    });
  }

  if (this->Files) {
    cm::erase_if(*(this->Files), [this](std::string const& arg) -> bool {
      if (!cmSystemTools::FileExists(arg)) {
        std::ostringstream e;
        e << "File \"" << arg << "\" does not exist. Cannot submit "
          << "a non-existent file.";
        this->Makefile->IssueMessage(MessageType::FATAL_ERROR, e.str());
        return true;
      }
      return false;
    });
  }
}
