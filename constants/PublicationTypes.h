//
// Created by Emmanuel Addo-Odame on 13/11/2025.
//

#ifndef PUBLICATIONTYPES_H
#define PUBLICATIONTYPES_H
namespace gnp::constants {

    enum PublicationTypes {
        Magazine = 0,
        News = 1,
        Archives = 2,
    };

    enum DifficultyMultipliers {
        EASY = 0,
        HARD = 1,
        MEDIUM = 2,
        EXPERT = 3,
    };

    enum GamePoints {
        BASE_POINTS_SUDOKU = 50,
        BASE_POINTS_WORDSEARCH = 40,
        BASE_POINTS_CROSSWORD = 60,
        BASE_POINTS_BRAINTEASER = 30,
        PERFECT_BONUS = 25,
        DAILY_CHALLENGE_BONUS = 100,
        MAX_STREAK_BONUS = 5,
        ACHIEVEMENT_BONUS = 50,
    };




}
#endif //PUBLICATIONTYPES_H
