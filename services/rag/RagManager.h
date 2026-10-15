// Retrieval-augmented question answering for the Graphic Archives.
#pragma once

#include <drogon/drogon.h>

#include "dto/BaseApiResponse.h"
#include "dto/RagQueryDto.h"

#include <string>

namespace gnp::services {

class RagManager {
public:
  // Retrieves OCR page evidence from newspaper_details and, when configured,
  // asks an OpenAI-compatible chat-completions endpoint for a grounded answer.
  // Retrieval and citations remain available if the generation provider is
  // not configured or temporarily unavailable.
  drogon::Task<dto::BaseApiResponse> answerArchiveQuestion(
      const dto::RagQueryDto &request,
      const std::string &userId = "");
};

} // namespace gnp::services
