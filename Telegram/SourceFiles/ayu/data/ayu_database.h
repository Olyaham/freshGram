// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ayu/data/entities.h"

#include <functional>

class SchemaVersion
{
public:
	int id;
	int version;
};

namespace AyuDatabase {

void initialize();

void addEditedMessage(const EditedMessage &message);
std::vector<EditedMessage> getEditedMessages(ID userId, ID dialogId, ID messageId, ID minId, ID maxId, int totalLimit);
bool hasRevisions(ID userId, ID dialogId, ID messageId);

void addDeletedMessage(const DeletedMessage &message);
void addDeletedMessages(const std::vector<DeletedMessage> &messages);
std::vector<DeletedMessage> getDeletedMessages(ID userId, ID dialogId, ID topicId, ID minId, ID maxId, int totalLimit, const std::string &searchQuery = "");
bool hasDeletedMessages(ID userId, ID dialogId, ID topicId);
std::vector<ID> getDeletedDialogIds(ID userId);
void removeDeletedMessage(ID userId, ID dialogId, ID messageId);
void clearDeletedMessages(ID userId, ID dialogId, ID topicId);

void saveSecretChat(const SecretChatRow &chat);
std::vector<SecretChatRow> getSecretChats(ID userId);
void removeSecretChat(ID userId, int chatId);
bool addSecretMessage(const SecretMessageRow &message);
std::vector<SecretMessageRow> getSecretMessages(ID userId, int chatId);
void removeSecretMessage(ID userId, int chatId, ID randomId);
void clearSecretMessages(ID userId, int chatId);

void saveKeptDialog(const KeptDialog &dialog);
std::vector<KeptDialog> getKeptDialogs(ID userId);
void removeKeptDialog(ID userId, ID dialogId);

std::vector<RegexFilter> getAllRegexFilters();
RegexFilter getById(std::vector<char> id);
std::vector<RegexFilter> getShared();
std::vector<RegexFilter> getByDialogId(ID dialogId);
std::vector<RegexFilterGlobalExclusion> getAllFiltersExclusions();
std::vector<RegexFilter> getExcludedByDialogId(ID dialogId);

int getCount();


void addRegexFilter(const RegexFilter &filter);
void addRegexExclusion(const RegexFilterGlobalExclusion &exclusion);

void updateRegexFilter(const RegexFilter &filter);

void deleteFilter(const std::vector<char> &id);
void deleteExclusionsByFilterId(const std::vector<char> &id);
void deleteExclusion(ID dialogId, std::vector<char> filterId);

void deleteAllFilters();
void deleteAllExclusions();

bool hasFilters();
bool hasPerDialogFilters();

void moveCurrentDatabase();
void backupCurrentDatabase();

}
