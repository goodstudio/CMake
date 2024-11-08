/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#pragma once

#include "cmConfigure.h" // IWYU pragma: keep

#include <memory>
#include <string>
#include <vector>

#include <cm/string_view>

#include "cmArgumentParser.h"
#include "cmCTestCommand.h"

class cmCTestGenericHandler;
class cmExecutionStatus;

/** \class cmCTestHandler
 * \brief Run a ctest script
 *
 * cmCTestHandlerCommand defineds the command to test the project.
 */
class cmCTestHandlerCommand
  : public cmCTestCommand
  , public cmArgumentParser<void>
{
public:
  /**
   * The name of the command as specified in CMakeList.txt.
   */
  virtual std::string GetName() const = 0;

  /**
   * This is called when the command is first encountered in
   * the CMakeLists.txt file.
   */
  bool InitialPass(std::vector<std::string> const& args,
                   cmExecutionStatus& status) override;

protected:
  virtual std::unique_ptr<cmCTestGenericHandler> InitializeHandler() = 0;

  virtual void ProcessAdditionalValues(cmCTestGenericHandler* /*handler*/) {}

  // Command argument handling.
  virtual void BindArguments();
  virtual void CheckArguments();

  std::vector<cm::string_view> ParsedKeywords;
  bool Append = false;
  bool Quiet = false;
  std::string CaptureCMakeError;
  std::string ReturnValue;
  std::string Build;
  std::string Source;
  std::string SubmitIndex;
};
