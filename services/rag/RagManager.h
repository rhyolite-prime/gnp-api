// Retrieval-augmented question answering for the Graphic Archives.
#pragma once

#include <drogon/drogon.h>

#include "dto/BaseApiResponse.h"
#include "dto/RagQueryDto.h"

#include <string>

namespace gnp::services {

class RagManager {
public:
  // Retrieves OCR page evidence from newspaper_details, optionally expands the
  // question with a local Ollama model, then asks that model for a grounded,
  // cited answer. Retrieval and citations remain available when Ollama is down.
  drogon::Task<dto::BaseApiResponse> answerArchiveQuestion(
      const dto::RagQueryDto &request,
      const std::string &userId = "");
};

} // namespace gnp::services
