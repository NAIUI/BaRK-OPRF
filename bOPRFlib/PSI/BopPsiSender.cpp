#include "BopPsiSender.h"
#include "Crypto/Commit.h"
#include "Common/Log.h"
#include "Common/Timer.h"
#include "OT/Base/naor-pinkas.h"

namespace bOPRF
{

	BopPsiSender::BopPsiSender()
	{
	}

	BopPsiSender::~BopPsiSender()
	{
	}
	extern std::string hexString(u8* data, u64 length);

	void BopPsiSender::init(u64 senderSize, u64 recverSize, u64 statSec, Channel & chl0, SSOtExtSender& ots, block seed, SimpleHasher* mBins)
	{
		init(senderSize, recverSize, statSec, { &chl0 }, ots, seed, mBins);
	}

	void BopPsiSender::init(u64 senderSize, u64 recverSize, u64 statSec, const std::vector<Channel*>& chls, SSOtExtSender& otSend, block seed, SimpleHasher* mBins)
	{
		mStatSecParam = statSec;
		mSenderSize = senderSize;
		mRecverSize = recverSize;
		mNumStash = get_stash_size(recverSize);

		// we need a random hash function, so both commit to a seed and then decommit later
		PRNG prngHashing(seed);
		block myHashSeeds;
		myHashSeeds = prngHashing.get_block();
		auto& chl0 = *chls[0];

		chl0.asyncSend(&myHashSeeds, sizeof(block));
		block theirHashingSeeds;
		chl0.asyncRecv(&theirHashingSeeds, sizeof(block));
		// init Simple hash
		mPsiRecvSSOtMessages.resize(mBins[0].mBinCount + mNumStash);

		//do base OT
		if (otSend.hasBaseSSOts() == false)
		{
			//Timer timer;
			BaseSSOT baseSSOTs(chl0, OTRole::Receiver);
			baseSSOTs.exec_base(prngHashing);
			baseSSOTs.check();
			otSend.setBaseSSOts(baseSSOTs.receiver_outputs, baseSSOTs.receiver_inputs);
			//	gTimer.setTimePoint("s baseDOne");
			mSSOtChoice = baseSSOTs.receiver_inputs;
			//Log::out << gTimer;
		}
		mHashingSeed = myHashSeeds ^ theirHashingSeeds;
		otSend.Extend(mBins[0].mBinCount + mNumStash, mPsiRecvSSOtMessages, chl0);
		gTimer.setTimePoint("s InitS.extFinished");
	}


	void BopPsiSender::sendInput(std::vector<std::vector<block>>& inputs, Channel & chl, SimpleHasher* mBins)
	{
		sendInput(inputs, { &chl }, mBins);
	}

	void BopPsiSender::sendInput(std::vector<std::vector<block>>& inputs, const std::vector<Channel*>& chls, SimpleHasher* mBins)
	{
		//gTimer.setTimePoint("OnlineS.start");
		PRNG prng(ZeroBlock);
		auto& chl = *chls[0];
		SHA1 sha1;
		u8 hashBuff[SHA1::HashSize];
		u64 maskSize = get_mask_size(mSenderSize, mRecverSize); //by byte
		u64 codeWordSize = get_codeword_size(std::max<u64>(mSenderSize, mRecverSize)); //by byte

		//compute PRC

		gTimer.setTimePoint("S Online.PRC start");
		std::array<AES, 4> AESHASH;
		TODO("make real keys seeds");
		for (u64 i = 0; i < AESHASH.size(); ++i)
			AESHASH[i].setKey(_mm_set1_epi64x(i));

		std::vector<std::array<std::vector<block>, 4>> aesHashBuffs(inputs.size());
		for (int i = 0; i < aesHashBuffs.size(); ++i)
		{
			aesHashBuffs[i][0].resize(inputs[i].size());
			aesHashBuffs[i][1].resize(inputs[i].size());
			aesHashBuffs[i][2].resize(inputs[i].size());
			aesHashBuffs[i][3].resize(inputs[i].size());
		}

		for (int ii = 0; ii < aesHashBuffs.size(); ++ii)
		{
			for (u64 i = 0; i < inputs[ii].size(); i += stepSize)
			{
				auto currentStepSize = std::min(stepSize, inputs[ii].size() - i);
				AESHASH[0].ecbEncBlocks(inputs[ii].data() + i, currentStepSize, aesHashBuffs[ii][0].data() + i);
				AESHASH[1].ecbEncBlocks(inputs[ii].data() + i, currentStepSize, aesHashBuffs[ii][1].data() + i);
				AESHASH[2].ecbEncBlocks(inputs[ii].data() + i, currentStepSize, aesHashBuffs[ii][2].data() + i);
				AESHASH[3].ecbEncBlocks(inputs[ii].data() + i, currentStepSize, aesHashBuffs[ii][3].data() + i);
			}
		}
		gTimer.setTimePoint("S Online.PRC done");

		//insert element into bin
		for (int i = 0; i < aesHashBuffs.size(); ++i) 
		{
			std::array<std::vector<block>, 4> aesHashBuff = aesHashBuffs[i];
			mBins[i].insertItems(aesHashBuff);
		}
		
		//OT value from office phasing	
		auto& blk448Choice = mSSOtChoice.getArrayView<blockBop>()[0];
		blockBop codeWord;

		//======================Bucket BINs (not stash)==========================		
		for (int k = 0; k < inputs.size(); ++k)
		{
			//u64 cntMask = mBins.mN;
			std::unique_ptr<ByteStream> myMaskBuff1(new ByteStream());
			std::unique_ptr<ByteStream> myMaskBuff2(new ByteStream());
			std::unique_ptr<ByteStream> myMaskBuff3(new ByteStream());
			myMaskBuff1->resize(mSenderSize* maskSize);
			myMaskBuff2->resize(mSenderSize* maskSize);
			myMaskBuff3->resize(mSenderSize* maskSize);

			//create permute array to add my mask in the permuted positions
			std::array<std::vector<u64>, 3> permute;
			int idxPermuteDone[3];
			for (u64 j = 0; j < 3; j++)
			{
				permute[j].resize(mSenderSize);
				for (u64 i = 0; i < mSenderSize; i++)
				{
					permute[j][i] = i;
				}
				//permute position
				std::shuffle(permute[j].begin(), permute[j].end(), prng);
				idxPermuteDone[j] = 0; //count the number of permutation that is done.
			}

			//pipelining the execution of the online phase (i.e., OT correction step) into multiple batches
			TODO("run in parallel");
			auto binStart = 0;
			u64 binEnd = 0;
			for (int i = 0; i < aesHashBuffs.size(); ++i) { binEnd = mBins[i].mBinCount; break;}
			gTimer.setTimePoint("S Online.computeBucketMask start");
			//for each batch
			for (u64 stepIdx = binStart; stepIdx < binEnd; stepIdx += stepSize)
			{
				// compute the  size of the current step and the end index
				auto currentStepSize = std::min(stepSize, binEnd - stepIdx);
				auto stepEnd = stepIdx + currentStepSize;
				// receive their  OT correction mask values.
				ByteStream theirCorrOTMasksBuff;
				chl.recv(theirCorrOTMasksBuff);
				// check the size
				if (theirCorrOTMasksBuff.size() != sizeof(blockBop)*currentStepSize)
					throw std::runtime_error("rt error at " LOCATION);

				auto theirCorrOT = theirCorrOTMasksBuff.getArrayView<blockBop>();
				// loop all the bins in this step.
				for (u64 bIdx = stepIdx, j = 0; bIdx < stepEnd; ++bIdx, ++j)
				{
					// current bin.			
					auto bin = mBins[k].mBins[bIdx];
					// for each item, hash it, encode then hash it again. 
					for (u64 i = 0; i < mBins[k].mBinSizes[bIdx]; ++i)
					{
						codeWord.elem[0] = aesHashBuffs[k][0][bin[i].mIdx];
						codeWord.elem[1] = aesHashBuffs[k][1][bin[i].mIdx];
						codeWord.elem[2] = aesHashBuffs[k][2][bin[i].mIdx];
						codeWord.elem[3] = aesHashBuffs[k][3][bin[i].mIdx];

						auto sum = mPsiRecvSSOtMessages[bIdx] ^ ((theirCorrOT[j] ^ codeWord) & blk448Choice);

						sha1.Reset();
						sha1.Update((u8*)&bin[i].mHashIdx, sizeof(u64)); //add hash index 
						sha1.Update((u8*)&sum, codeWordSize);
						sha1.Final(hashBuff);

						//put the mask into corresponding buff at the permuted position
						if (bin[i].mHashIdx == 0) 	//buff 1 for hash index 0		
							memcpy(myMaskBuff1->data() + permute[0][idxPermuteDone[0]++] * maskSize, hashBuff, maskSize);
						else if (bin[i].mHashIdx == 1)//buff 2 for hash index 1		
							memcpy(myMaskBuff2->data() + permute[1][idxPermuteDone[1]++] * maskSize, hashBuff, maskSize);
						else if (bin[i].mHashIdx == 2)//buff 3 for hash index 2
							memcpy(myMaskBuff3->data() + permute[2][idxPermuteDone[2]++] * maskSize, hashBuff, maskSize);
					}
				}	
			}
			gTimer.setTimePoint("S Online.computeBucketMask done");
			chl.asyncSend(std::move(myMaskBuff1));
			chl.asyncSend(std::move(myMaskBuff2));
			chl.asyncSend(std::move(myMaskBuff3));
			gTimer.setTimePoint("S Online.sendBucketMask done");
		}
		//======================STASH BIN==========================
		//receive theirStashCorrOTMasksBuff
		ByteStream theirStashCorrOTMasksBuff;	
		chl.recv(theirStashCorrOTMasksBuff);	
		auto theirStashCorrOT = theirStashCorrOTMasksBuff.getArrayView<blockBop>();
		if (theirStashCorrOT.size() != mNumStash)
			throw std::runtime_error("rt error at " LOCATION);
		// now compute mask for each of the stash elements
		for (u64 k = 0; k < inputs.size(); ++k)
		{
			u64 otIdx = 0;
			for (int i = 0; i < aesHashBuffs.size(); ++i) { otIdx = mBins[i].mBinCount; break;}
			for (u64 stashIdx = 0; stashIdx < mNumStash; ++stashIdx, ++otIdx)
			{		
				std::unique_ptr<ByteStream> myStashMasksBuff(new ByteStream());
				myStashMasksBuff->resize(mSenderSize* maskSize);

				//cntMask = mSenderSize;
				std::vector<u64> stashPermute(mSenderSize);
				int idxStashDone = 0;
				for (u64 i = 0; i < mSenderSize; i++)
					stashPermute[i] = i;

				//permute position
				std::shuffle(stashPermute.begin(), stashPermute.end(), prng);

				//compute mask
				auto aesHashBuff = aesHashBuffs.at(k);
				for (u64 i = 0; i < inputs[k].size(); ++i)
				{
					codeWord.elem[0] = aesHashBuff[0][i];
					codeWord.elem[1] = aesHashBuff[1][i];
					codeWord.elem[2] = aesHashBuff[2][i];
					codeWord.elem[3] = aesHashBuff[3][i];
					codeWord = mPsiRecvSSOtMessages[otIdx] ^ ((theirStashCorrOT[stashIdx] ^ codeWord) & blk448Choice);
					sha1.Reset();
					sha1.Update((u8*)&codeWord, codeWordSize);
					sha1.Final(hashBuff);
					// copy mask into the buffer in permuted pos
					memcpy(myStashMasksBuff->data() + stashPermute[idxStashDone++] * maskSize, hashBuff, maskSize);
				}
				chl.asyncSend(std::move(myStashMasksBuff));
			}
		}
	}
}


