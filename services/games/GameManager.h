//
// Created by Emmanuel Addo-Odame on 27/08/2026.
//

#ifndef GNPAPI_GAMESERVICE_H
#define GNPAPI_GAMESERVICE_H

#include <drogon/drogon.h>

#include "dto/BaseApiResponse.h"
#include "services/games/GameTypes.h"

namespace gnp::services {

    // Forward declarations
    struct GameSession;
    struct UserStats;
    struct LeaderboardEntry;
    struct Achievement;
    struct GameReward;

    // Game session structure to track active games
    struct GameSession {
        std::string sessionId;
        std::string userId;
        std::string gameType;
        std::string puzzleId;
        std::chrono::system_clock::time_point startTime;
        std::chrono::system_clock::time_point lastActivity;
        int currentScore = 0;
        int totalPoints = 0;
        std::string difficulty;
        Json::Value progressData;
        bool isMultiplayer = false;
        std::string opponentId;
        bool isComplete = false;
        int mistakes = 0;
        int hintsUsed = 0;
        int completionTimeSeconds = 0;
    };

    // User statistics structure
    struct UserStats {
        std::string userId;
        std::string username;
        int totalPoints = 0;
        int gamesPlayed = 0;
        int gamesWon = 0;
        int gamesLost = 0;
        int gamesDraw = 0;
        int currentStreak = 0;
        int longestStreak = 0;
        double winRate = 0.0;
        std::map<std::string, int> gameTypePoints;
        std::map<std::string, int> gameTypePlayed;
        std::vector<std::string> achievements;
        std::chrono::system_clock::time_point lastPlayed;
    };

    // Leaderboard entry
    struct LeaderboardEntry {
        std::string userId;
        std::string username;
        int totalPoints = 0;
        int gamesPlayed = 0;
        double winRate = 0.0;
        std::chrono::system_clock::time_point lastActive;
        int rank = 0;
    };

    // Achievement structure
    struct Achievement {
        std::string id;
        std::string name;
        std::string description;
        int pointsRequired = 0;
        int gamesRequired = 0;
        std::string gameType;
        std::string achievementType;
        bool isUnlocked = false;
        std::chrono::system_clock::time_point unlockedAt;
        int bonusPoints = 0;
    };

    // Game reward structure
    struct GameReward {
        int pointsEarned = 0;
        int bonusPoints = 0;
        bool isNewAchievement = false;
        std::string achievementName;
        std::string message;
        std::vector<std::string> newAchievements;
    };



    class GameManager {
    public:


        // ============= Get Game Types =============

        drogon::Task<dto::BaseApiResponse> getAllGameCategories();

        // ============= Session Management =============

        // Start a new game session
        drogon::Task<GameSession> startGameSession(
            const std::string& userId,
            const std::string& gameType,
            const std::string& difficulty = "easy",
            bool isMultiplayer = false,
            const std::string& opponentId = ""
        );

        // End a game session and calculate points
        drogon::Task<GameReward> endGameSession(
            const std::string& sessionId,
            bool isCompleted,
            int completionTimeSeconds = 0,
            int mistakes = 0,
            int hintsUsed = 0,
            const Json::Value& finalProgress = Json::Value(Json::objectValue)
        );

        // Get current game session for a user
        drogon::Task<std::optional<GameSession>> getCurrentSession(
            const std::string& userId
        );

        // Save progress for a game session
        drogon::Task<bool> saveProgress(
            const std::string& sessionId,
            const Json::Value& progress
        );

        drogon::Task<bool> deleteSession(const std::string& sessionId);

        // ============= Point Accumulation =============

        // Add points to user's total
        drogon::Task<bool> addPoints(
            const std::string& userId,
            int points,
            const std::string& reason
        );

        // Get user's total points
        drogon::Task<int> getUserPoints(const std::string& userId);

        // Get detailed user stats
        drogon::Task<UserStats> getUserStats(const std::string& userId);

        // Get user's game history
        drogon::Task<std::vector<GameSession>> getGameHistory(
            const std::string& userId,
            int pageNo = 1,
            int pageSize = 10
        );

        // ============= Achievement System =============

        // Check and award achievements for a user
        drogon::Task<std::vector<Achievement>> checkAchievements(
            const std::string& userId
        );

        // Get all available achievements
        drogon::Task<std::vector<Achievement>> getAvailableAchievements();

        // Get user's unlocked achievements
        drogon::Task<std::vector<Achievement>> getUserAchievements(
            const std::string& userId
        );

        // ============= Leaderboard =============

        // Get global leaderboard
        drogon::Task<std::vector<LeaderboardEntry>> getGlobalLeaderboard(
        int pageNo = 1,
        int pageSize = 10
        );

        drogon::Task<std::vector<LeaderboardEntry>> getGameTypeLeaderboard(
            const std::string& gameType,
            int pageNo = 1,
            int pageSize = 10
        );

        drogon::Task<std::vector<LeaderboardEntry>> getFriendsLeaderboard(
            const std::string& userId,
            int limit = 50
        );


        // ============= Multiplayer Support =============

        // Challenge another user to a game
        drogon::Task<bool> challengeUser(
            const std::string& challengerId,
            const std::string& opponentId,
            const std::string& gameType
        );

        drogon::Task<bool> acceptChallenge(
            const std::string& challengeId,
            const std::string& userId
        );

        drogon::Task<GameReward> submitMultiplayerResult(
            const std::string& sessionId,
            const std::string& winnerId,
            const std::map<std::string, int>& playerScores
        );

        // ============= Game Generation (Enhanced) =============

        // Generate and start a new Sudoku game
        drogon::Task<GameSession> generateSudoku(const std::string& userId);

        drogon::Task<GameSession> generateWordSearch(const std::string& userId);

        drogon::Task<GameSession> generateWordSearchTopics(
            const std::string& userId,
            const std::vector<std::string>& topics
        );

        // Generate and start a new Crossword puzzle
        drogon::Task<GameSession> generateCrosswordPuzzle(const std::string& userId);

        // Generate a Brain Teaser
        drogon::Task<GameSession> generateBrainTeaser(const std::string& userId);

        // ============= Point Calculation Rules =============

        // Calculate points for completing a puzzle
        drogon::Task<int> calculatePuzzlePoints(
            const std::string& gameType,
            const std::string& difficulty,
            double completionTime,
            int mistakes = 0,
            int hintsUsed = 0
        );

        drogon::Task<int> calculatePerfectBonus(
            const std::string& gameType,
            const std::string& difficulty,
            int mistakes,
            int hintsUsed
        );

        // Calculate streak bonus
        drogon::Task<int> calculateStreakBonus(const std::string& userId);


        // ============= Daily Challenges =============

        // Get today's daily challenge
        drogon::Task<std::optional<GameSession>> getDailyChallenge(
            const std::string& userId
        );

        // Complete daily challenge and get bonus points
        drogon::Task<GameReward> completeDailyChallenge(
            const std::string& userId,
            const std::string& sessionId
        );

        // ============= Utility Methods =============

        // Reset user progress (for testing or user request)
        drogon::Task<bool> resetUserProgress(const std::string& userId);

        // Get game statistics for a specific game type
        drogon::Task<std::map<std::string, int>> getGameStatistics(
            const std::string& gameType
        );

        // Check if user is eligible for a specific achievement
        drogon::Task<bool> isEligibleForAchievement(
            const std::string& userId,
            const std::string& achievementId
        );

        // Award a specific achievement
        drogon::Task<bool> awardAchievement(
            const std::string& userId,
            const std::string& achievementId
        );

        // ============= HTTP-facing operations =============
        // Controllers should use these. They redact solutions and return BaseApiResponse.

        drogon::Task<dto::BaseApiResponse> startGame(const StartOptions &options);
        drogon::Task<dto::BaseApiResponse> getSessionView(const std::string &userId, const std::string &sessionId);
        drogon::Task<dto::BaseApiResponse> getCurrentSessionView(const std::string &userId);
        drogon::Task<dto::BaseApiResponse> saveSessionProgress(const std::string &userId, const std::string &sessionId, const Json::Value &playerState);
        drogon::Task<dto::BaseApiResponse> submitSession(const std::string &userId, const std::string &sessionId, const Json::Value &submission);
        drogon::Task<dto::BaseApiResponse> takeHint(const std::string &userId, const std::string &sessionId);
        drogon::Task<dto::BaseApiResponse> removeSession(const std::string &userId, const std::string &sessionId);
        drogon::Task<dto::BaseApiResponse> historyView(const std::string &userId, int pageNo, int pageSize);
        drogon::Task<dto::BaseApiResponse> statsView(const std::string &userId);
        drogon::Task<dto::BaseApiResponse> pointsView(const std::string &userId);
        drogon::Task<dto::BaseApiResponse> achievementsView(const std::string &userId, bool mineOnly);
        drogon::Task<dto::BaseApiResponse> leaderboardView(const std::string &gameType, const std::string &period, int pageNo, int pageSize, const std::string &focusUserId);
        drogon::Task<dto::BaseApiResponse> rivalsView(const std::string &userId, int limit);
        drogon::Task<dto::BaseApiResponse> createChallenge(const std::string &userId, const std::string &opponentId, const std::string &gameType, const std::string &difficulty);
        drogon::Task<dto::BaseApiResponse> acceptChallengeView(const std::string &userId, const std::string &challengeId);
        drogon::Task<dto::BaseApiResponse> declineChallengeView(const std::string &userId, const std::string &challengeId);
        drogon::Task<dto::BaseApiResponse> listChallengesView(const std::string &userId);
        drogon::Task<dto::BaseApiResponse> finishMultiplayer(const std::string &userId, const std::string &sessionId);
        drogon::Task<dto::BaseApiResponse> dailyBoard(const std::string &userId);
        drogon::Task<dto::BaseApiResponse> startDaily(const std::string &userId, const std::string &challengeId, const std::string &gameType);
        drogon::Task<dto::BaseApiResponse> completeDailyView(const std::string &userId, const std::string &sessionId);
        drogon::Task<dto::BaseApiResponse> dailyLeaderboardView(int pageNo, int pageSize);
        drogon::Task<dto::BaseApiResponse> publicationSourcesView(const std::string &publicationId);
        drogon::Task<dto::BaseApiResponse> statisticsView(const std::string &gameType);
        drogon::Task<dto::BaseApiResponse> resetProgressView(const std::string &userId);
        drogon::Task<dto::BaseApiResponse> seedAchievementsView();
        drogon::Task<dto::BaseApiResponse> refreshLexiconView();

    private:
        // ============= Private Helper Methods =============

        // Generate unique ID
        std::string generateSessionId();

        drogon::Task<void> ensureAchievementCatalog();
        drogon::Task<GameSession> openSession(const StartOptions& options);
        drogon::Task<std::string> ensureDailyChallenges();

        // Calculate points based on difficulty
        int getDifficultyMultiplier(const std::string& difficulty);

        // Get point values for game types
        int getBasePoints(const std::string& gameType);

        // Update user statistics after game completion
        drogon::Task<bool> updateUserStats(
            const std::string& userId,
            const GameSession& session,
            const GameReward& reward
        );

        // Check for new achievements
        drogon::Task<std::vector<Achievement>> checkAndAwardAchievements(
            const std::string& userId,
            const UserStats& stats
        );

        // Initialize achievement definitions
        void initializeAchievements();

        // Validate game session
        bool isValidSession(const GameSession& session);

        // Calculate win rate
        double calculateWinRate(const UserStats& stats);

        // Update leaderboard
        drogon::Task<bool> updateLeaderboard(
            const std::string& userId,
            const GameReward& reward
        );



    };

}


#endif //GNPAPI_GAMESERVICE_H
